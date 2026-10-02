// Structured fuzzer for the sqlean extensions that libsql-ffi compiles into sqld.
//
// Built and run by `mise run fuzz:sqlean`: SQLite and sqlean come from the patched sources in
// .build/libsql, compiled with the flags of libsql-ffi's build.rs plus AddressSanitizer and
// UndefinedBehaviorSanitizer. Every function, table-valued module and collation sqlean registers
// is discovered at startup and called with every edge value in every argument position, with
// every pair of non-numeric edge values when it takes two arguments, and then with random
// arguments until the time budget runs out. A sanitizer report or a statement running longer
// than HANG_SECONDS ends the run after printing that statement as SQL.
//
// usage: sqlean-fuzz <seconds> [seed]

#include "sqlite3.c"
SQLITE_EXTENSION_INIT1

#include "crypto/extension.h"
#include "fuzzy/extension.h"
#include "math/extension.h"
#include "regexp/extension.h"
#include "sqlean.h"
#include "stats/extension.h"
#include "text/extension.h"
#include "uuid/extension.h"

#include <fcntl.h>
#include <math.h>
#include <sanitizer/common_interface_defs.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define HANG_SECONDS 10
#define MAX_ARGS 6
#define MAX_ROWS 1000

// Same registration as the sqlite3-sqlean-generated.c that libsql-ffi's build.rs writes.
static void sqlean_version(sqlite3_context* context, int argc, sqlite3_value** argv) {
    sqlite3_result_text(context, SQLEAN_VERSION, -1, SQLITE_STATIC);
}

static int sqlite3_sqlean_init(sqlite3* db, char** errmsg, const sqlite3_api_routines* api) {
    (void)errmsg;
    SQLITE_EXTENSION_INIT2(api);
    static const int flags = SQLITE_UTF8 | SQLITE_INNOCUOUS | SQLITE_DETERMINISTIC;
    sqlite3_create_function(db, "sqlean_version", 0, flags, 0, sqlean_version, 0, 0);
    crypto_init(db);
    fuzzy_init(db);
    math_init(db);
    stats_init(db);
    text_init(db);
    uuid_init(db);
    regexp_init(db);
    return SQLITE_OK;
}

// --- values ---------------------------------------------------------------------------------

typedef enum { V_NULL, V_INT, V_REAL, V_TEXT, V_BLOB } kind_t;

typedef struct {
    kind_t kind;
    sqlite3_int64 i;
    double r;
    const unsigned char* bytes;  // TEXT/BLOB, not NUL-terminated
    int len;
} value_t;

#define T(s) {V_TEXT, 0, 0, (const unsigned char*)(s), (int)sizeof(s) - 1}
#define B(s) {V_BLOB, 0, 0, (const unsigned char*)(s), (int)sizeof(s) - 1}
#define I(n) {V_INT, (n), 0, NULL, 0}
#define R(x) {V_REAL, 0, (x), NULL, 0}

static const value_t EDGE_VALUES[] = {
    {V_NULL, 0, 0, NULL, 0},
    I(0), I(1), I(-1), I(2), I(3), I(4), I(5), I(7), I(8), I(-2), I(15), I(16), I(31), I(32),
    I(63), I(64), I(100), I(-100), I(127), I(128), I(255), I(256), I(1000), I(4096), I(65535),
    I(65536), I(1000000), I(2147483647), I(-2147483647 - 1), I(4294967296LL),
    I(9223372036854775807LL), I(-9223372036854775807LL - 1),
    R(0.0), R(-0.0), R(0.5), R(-0.5), R(1.5), R(3.14159), R(1e-310), R(1e308), R(-1e308),
    R(1e18), R(-1e18), R(INFINITY), R(-INFINITY),
    T(""), T("a"), T("abc"), T("hello world"), T(" "), T("\t\n"), T("%"), T("_"), T("%%"),
    T("\\"), T("a%b_c"), T("%a"), T("a%"), T("*"), T(".*"), T("(a+)+$"), T("(a|aa)*b"), T("["),
    T("("), T(")"), T("?"), T("{1,1000}"), T("^$"), T("\\1"), T("(?<n>a)\\k<n>"), T("[[:alpha:]]"),
    T("Привет мир"), T("ЁЖ ё е"), T("ß"), T("ẞ"), T("ſ"), T("İ"), T("ı"), T("ﬃ"), T("Σσς"),
    T("ǅ"), T("ᾈ"), T("😀"), T("a\xcc\x81"), T("x\0y"), T("\0"),
    T("\x80"), T("\xbf"), T("\xff"), T("\xfe\xff"), T("\xc3"), T("\xe2\x82"), T("\xf0\x9f\x98"),
    T("\xed\xa0\x80"), T("\xc0\x80"), T("\xc1\xbf"), T("\xf4\x90\x80\x80"), T("\xf8\x88\x80\x80\x80"),
    T("a\x80" "b"), T("\xd0"), T("\xd0\xd0\xd0"),
    T("z"), T("zzzzz"), T("z!!"), T("!!z"), T("~~~~~"), T("<~"), T("~>"), T("s8W-!"), T("====="),
    T("MZXW6==="), T("Zm9v"), T("Zm9"), T("Z"), T("=="), T("0x"), T("abc"), T("0g"), T("f"),
    T("%41"), T("%4"), T("%zz"), T("%"), T("+"),
    T("base32"), T("base64"), T("base85"), T("hex"), T("url"), T("md5"), T("sha1"), T("sha256"),
    T("sha384"), T("sha512"), T("blake3"), T("xxhash"),
    T("00000000-0000-0000-0000-000000000000"), T("{00000000-0000-0000-0000-000000000000}"),
    T("ffffffffffffffffffffffffffffffff"), T("not-a-uuid"),
    T("1"), T("-1"), T("1e309"), T("nan"), T("inf"), T("0.0"), T("12345678901234567890"),
    B(""), B("\x00"), B("\xff"), B("\x00\x01\x02\x03"), B("\xde\xad\xbe\xef\x00\x00\x00\x00"),
};
#define N_EDGE (int)(sizeof(EDGE_VALUES) / sizeof(EDGE_VALUES[0]))

// Long values are built at startup.
#define N_LONG 8
static value_t LONG_VALUES[N_LONG];

static void make_long_values(void) {
    static const char* units[N_LONG] = {"a", "Я", "ẞ", "z", "%", "\xff", "ſ", "ab%"};
    static const int counts[N_LONG] = {5000, 3000, 2000, 5000, 1000, 1000, 3000, 2000};
    for (int k = 0; k < N_LONG; k++) {
        size_t ulen = strlen(units[k]);
        unsigned char* buf = malloc(ulen * counts[k]);
        for (int j = 0; j < counts[k]; j++) {
            memcpy(buf + j * ulen, units[k], ulen);
        }
        LONG_VALUES[k] = (value_t){V_TEXT, 0, 0, buf, (int)(ulen * counts[k])};
    }
}

// Default values for the other arguments while one position cycles through the edge values.
static const value_t DEFAULTS[] = {T("abc"), I(1), I(3), T("Привет")};
#define N_DEFAULTS (int)(sizeof(DEFAULTS) / sizeof(DEFAULTS[0]))

// --- random generation ----------------------------------------------------------------------

static unsigned long long rng_state;

static unsigned long long rnd(void) {
    // xorshift64*
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 2685821657736338717ULL;
}

static int rnd_below(int n) {
    return (int)(rnd() % (unsigned long long)n);
}

static const char* ALPHABET[] = {
    "a", "b", "z", "A", "Z", "0", "9", " ", "%", "_", "\\", ".", "*", "+", "?", "(", ")", "[",
    "]", "{", "}", "|", "^", "$", "-", "=", "!", "~", "<", ">", "/", ":", "\n", "Я", "я", "Ё",
    "ё", "ß", "ẞ", "ſ", "İ", "ı", "Σ", "ς", "ﬃ", "😀", "\xcc\x81", "\x80", "\xff", "\xc3",
    "\xe2\x82", "\xf0\x9f", "\xed\xa0\x80", "\0",
};
#define N_ALPHABET (int)(sizeof(ALPHABET) / sizeof(ALPHABET[0]))

// Random TEXT/BLOB contents live in buf (owned by the caller, at least 8 KiB).
static value_t random_value(unsigned char* buf) {
    int choice = rnd_below(10);
    if (choice < 3) {
        return EDGE_VALUES[rnd_below(N_EDGE)];
    }
    if (choice == 3) {
        return LONG_VALUES[rnd_below(N_LONG)];
    }
    if (choice == 4) {
        static const sqlite3_int64 ranges[] = {10, 300, 70000, 2000000000LL};
        sqlite3_int64 range = ranges[rnd_below(4)];
        return (value_t){V_INT, (sqlite3_int64)(rnd() % (2 * range + 1)) - range, 0, NULL, 0};
    }
    if (choice == 5) {
        double x = ((double)(rnd() % 2000000) - 1000000.0) / (double)(1 + rnd_below(1000));
        return (value_t){V_REAL, 0, x, NULL, 0};
    }
    if (choice == 6) {
        int len = rnd_below(64);
        for (int j = 0; j < len; j++) {
            buf[j] = (unsigned char)rnd();
        }
        return (value_t){V_BLOB, 0, 0, buf, len};
    }
    // Text from the alphabet, mostly short.
    int max_len = rnd_below(8) == 0 ? 2000 : 40;
    int n = rnd_below(max_len + 1), pos = 0;
    for (int j = 0; j < n; j++) {
        const char* unit = ALPHABET[rnd_below(N_ALPHABET)];
        size_t ulen = unit[0] == '\0' ? 1 : strlen(unit);
        if (pos + (int)ulen > 8000) {
            break;
        }
        memcpy(buf + pos, unit, ulen);
        pos += (int)ulen;
    }
    return (value_t){V_TEXT, 0, 0, buf, pos};
}

// --- targets --------------------------------------------------------------------------------

typedef enum { K_SCALAR, K_AGGREGATE, K_WINDOW, K_MODULE, K_COLLATION } target_kind;

typedef struct {
    char name[64];
    int narg;  // -1: variadic
    target_kind kind;
    long calls;
} target_t;

static target_t targets[512];
static int n_targets;

static int has_name(sqlite3* db, const char* sql, const char* name) {
    sqlite3_stmt* stmt;
    int found = 0;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = 1;
    }
    sqlite3_finalize(stmt);
    return found;
}

static void add_target(const char* name, int narg, target_kind kind) {
    if (n_targets == (int)(sizeof(targets) / sizeof(targets[0]))) {
        return;
    }
    target_t* t = &targets[n_targets++];
    snprintf(t->name, sizeof(t->name), "%s", name);
    t->narg = narg;
    t->kind = kind;
    t->calls = 0;
}

// plain is a connection opened before sqlean was registered: whatever exists only in db is
// sqlean's.
static void discover(sqlite3* plain, sqlite3* db) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT name, narg, type FROM pragma_function_list WHERE builtin = 0",
                       -1, &stmt, NULL);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = (const char*)sqlite3_column_text(stmt, 0);
        const char* type = (const char*)sqlite3_column_text(stmt, 2);
        target_kind kind = type[0] == 'a' ? K_AGGREGATE : type[0] == 'w' ? K_WINDOW : K_SCALAR;
        add_target(name, sqlite3_column_int(stmt, 1), kind);
    }
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db, "SELECT name FROM pragma_module_list", -1, &stmt, NULL);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = (const char*)sqlite3_column_text(stmt, 0);
        if (!has_name(plain, "SELECT 1 FROM pragma_module_list WHERE name = ?1", name)) {
            add_target(name, -1, K_MODULE);
        }
    }
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db, "SELECT name FROM pragma_collation_list", -1, &stmt, NULL);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = (const char*)sqlite3_column_text(stmt, 0);
        if (!has_name(plain, "SELECT 1 FROM pragma_collation_list WHERE name = ?1", name)) {
            add_target(name, 2, K_COLLATION);
        }
    }
    sqlite3_finalize(stmt);
}

// --- reproducer -------------------------------------------------------------------------------

// The statement being executed, with its arguments inlined as SQL literals. Kept in a static
// buffer so the death callback and the hang handler can print it without allocating.
static char repro[1 << 20];
static size_t repro_len;
static const char* repro_path;

static void repro_append(const char* s, size_t n) {
    if (repro_len + n >= sizeof(repro) - 1) {
        n = sizeof(repro) - 1 - repro_len;
    }
    memcpy(repro + repro_len, s, n);
    repro_len += n;
    repro[repro_len] = '\0';
}

static void repro_value(const value_t* v) {
    char tmp[64];
    switch (v->kind) {
        case V_NULL:
            repro_append("NULL", 4);
            break;
        case V_INT:
            repro_append(tmp, (size_t)snprintf(tmp, sizeof(tmp), "%lld", (long long)v->i));
            break;
        case V_REAL:
            if (isnan(v->r)) {
                repro_append("NULL", 4);
            } else if (isinf(v->r)) {
                repro_append(v->r > 0 ? "9e999" : "-9e999", v->r > 0 ? 5 : 6);
            } else {
                repro_append(tmp, (size_t)snprintf(tmp, sizeof(tmp), "%.17g", v->r));
            }
            break;
        case V_TEXT:
        case V_BLOB:
            // TEXT as CAST(X'..' AS TEXT) keeps invalid UTF-8 and embedded NULs exact.
            if (v->kind == V_TEXT) {
                repro_append("CAST(", 5);
            }
            repro_append("X'", 2);
            for (int j = 0; j < v->len; j++) {
                repro_append(tmp, (size_t)snprintf(tmp, sizeof(tmp), "%02x", v->bytes[j]));
            }
            repro_append("'", 1);
            if (v->kind == V_TEXT) {
                repro_append(" AS TEXT)", 9);
            }
            break;
    }
}

// Renders sql, replacing each "?" with the next value.
static void repro_set(const char* sql, const value_t* values, int n_values) {
    repro_len = 0;
    int next = 0;
    for (const char* p = sql; *p; p++) {
        if (*p == '?' && next < n_values) {
            repro_value(&values[next++]);
        } else {
            repro_append(p, 1);
        }
    }
}

static void write_all(int fd, const char* s, size_t n) {
    while (n > 0) {
        ssize_t w = write(fd, s, n);
        if (w <= 0) {
            return;
        }
        s += w;
        n -= (size_t)w;
    }
}

static void dump_repro(const char* why) {
    write_all(2, "\n=== ", 5);
    write_all(2, why, strlen(why));
    write_all(2, " while running:\n", 16);
    write_all(2, repro, repro_len);
    write_all(2, ";\n", 2);
    int fd = open(repro_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write_all(fd, repro, repro_len);
        write_all(fd, ";\n", 2);
        close(fd);
    }
}

static void on_sanitizer_death(void) {
    dump_repro("sanitizer report");
}

static void on_alarm(int sig) {
    (void)sig;
    dump_repro("HANG (statement ran longer than the hang limit)");
    abort();
}

// --- execution --------------------------------------------------------------------------------

static long n_statements, n_errors;

static void bind_value(sqlite3_stmt* stmt, int idx, const value_t* v) {
    switch (v->kind) {
        case V_NULL:
            sqlite3_bind_null(stmt, idx);
            break;
        case V_INT:
            sqlite3_bind_int64(stmt, idx, v->i);
            break;
        case V_REAL:
            sqlite3_bind_double(stmt, idx, v->r);
            break;
        case V_TEXT:
            sqlite3_bind_text(stmt, idx, (const char*)v->bytes, v->len, SQLITE_TRANSIENT);
            break;
        case V_BLOB:
            sqlite3_bind_blob(stmt, idx, v->bytes, v->len, SQLITE_TRANSIENT);
            break;
    }
}

static void run(sqlite3* db, const char* sql, const value_t* values, int n_values) {
    repro_set(sql, values, n_values);
    struct itimerval hang = {{0, 0}, {HANG_SECONDS, 0}}, off = {{0, 0}, {0, 0}};
    setitimer(ITIMER_REAL, &hang, NULL);

    sqlite3_stmt* stmt = NULL;
    n_statements++;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        n_errors++;
    } else {
        for (int j = 0; j < n_values; j++) {
            bind_value(stmt, j + 1, &values[j]);
        }
        int rc, rows = 0;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW && rows++ < MAX_ROWS) {
            // Reading every column as text and as blob exercises the result conversions.
            for (int c = 0; c < sqlite3_column_count(stmt); c++) {
                (void)sqlite3_column_text(stmt, c);
                (void)sqlite3_column_bytes(stmt, c);
            }
        }
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
            n_errors++;
        }
    }
    sqlite3_finalize(stmt);
    setitimer(ITIMER_REAL, &off, NULL);
}

// Builds the statement for target t with n call arguments and runs it. values holds the call
// arguments; aggregates and windows get them as one row each over three rows, with the first
// argument varied by the extra values in rows[0..1].
static void call(sqlite3* db, target_t* t, const value_t* args, int n, const value_t* rows) {
    char sql[512];
    char params[3 * MAX_ARGS + 1] = "";
    value_t values[MAX_ARGS + 4];
    for (int j = 0; j < n; j++) {
        strcat(params, j == 0 ? "?" : ", ?");
    }
    t->calls++;
    switch (t->kind) {
        case K_SCALAR:
            snprintf(sql, sizeof(sql), "SELECT %s(%s)", t->name, params);
            run(db, sql, args, n);
            break;
        case K_AGGREGATE:
        case K_WINDOW: {
            // the first argument comes from the rows, the others are bound once
            char fn_call[128];
            snprintf(fn_call, sizeof(fn_call), n == 0 ? "%s()" : "%s(x%s)", t->name,
                     n > 1 ? params + 1 : "");
            snprintf(sql, sizeof(sql), "WITH v(x) AS (VALUES (?), (?), (?)) SELECT %s FROM v",
                     fn_call);
            values[0] = n > 0 ? args[0] : rows[0];
            values[1] = rows[0];
            values[2] = rows[1];
            for (int j = 1; j < n; j++) {
                values[2 + j] = args[j];
            }
            int n_values = 3 + (n > 1 ? n - 1 : 0);
            run(db, sql, values, n_values);
            if (t->kind == K_WINDOW) {
                snprintf(sql, sizeof(sql),
                         "WITH v(x) AS (VALUES (?), (?), (?)) "
                         "SELECT %s OVER (ROWS BETWEEN 1 PRECEDING AND CURRENT ROW) FROM v",
                         fn_call);
                run(db, sql, values, n_values);
            }
            break;
        }
        case K_MODULE:
            snprintf(sql, sizeof(sql), "SELECT * FROM %s(%s) LIMIT 100", t->name, params);
            run(db, sql, args, n);
            break;
        case K_COLLATION: {
            snprintf(sql, sizeof(sql), "SELECT ? = ? COLLATE %s, ? < ? COLLATE %s", t->name,
                     t->name);
            values[0] = args[0];
            values[1] = args[1];
            values[2] = args[0];
            values[3] = args[1];
            run(db, sql, values, 4);
            snprintf(sql, sizeof(sql),
                     "WITH v(x) AS (VALUES (?), (?), (?), (?)) SELECT x FROM v ORDER BY x COLLATE %s",
                     t->name);
            values[2] = rows[0];
            values[3] = rows[1];
            run(db, sql, values, 4);
            break;
        }
    }
}

static int arg_counts(const target_t* t, int* counts) {
    if (t->kind == K_COLLATION) {
        counts[0] = 2;
        return 1;
    }
    if (t->narg > MAX_ARGS) {
        return 0;
    }
    if (t->narg >= 0) {
        counts[0] = t->narg;
        return 1;
    }
    for (int j = 0; j <= 4; j++) {
        counts[j] = j;
    }
    return 5;
}

// Every edge value in every argument position, the other arguments set to each default.
static void edge_pass(sqlite3* db) {
    value_t args[MAX_ARGS];
    value_t rows[2] = {EDGE_VALUES[1], EDGE_VALUES[2]};
    for (int ti = 0; ti < n_targets; ti++) {
        target_t* t = &targets[ti];
        int counts[5];
        int n_counts = arg_counts(t, counts);
        for (int ci = 0; ci < n_counts; ci++) {
            int n = counts[ci];
            if (n == 0) {
                call(db, t, args, 0, rows);
                continue;
            }
            for (int d = 0; d < N_DEFAULTS; d++) {
                for (int pos = 0; pos < n && pos < MAX_ARGS; pos++) {
                    for (int e = 0; e < N_EDGE + N_LONG; e++) {
                        for (int j = 0; j < n; j++) {
                            args[j] = DEFAULTS[d];
                        }
                        args[pos] = e < N_EDGE ? EDGE_VALUES[e] : LONG_VALUES[e - N_EDGE];
                        call(db, t, args, n, rows);
                    }
                }
            }
        }
    }
}

// Two-argument targets with every pair of non-numeric edge values: parsers usually take their
// input and its format or pattern together (crypto_decode('zzzzz', 'base85')), which a single
// varied position never combines.
static void pair_pass(sqlite3* db) {
    static value_t pool[N_EDGE + N_LONG];
    int n_pool = 0;
    for (int e = 0; e < N_EDGE; e++) {
        if (EDGE_VALUES[e].kind != V_INT && EDGE_VALUES[e].kind != V_REAL) {
            pool[n_pool++] = EDGE_VALUES[e];
        }
    }
    for (int e = 0; e < N_LONG; e++) {
        pool[n_pool++] = LONG_VALUES[e];
    }

    value_t args[2];
    value_t rows[2] = {EDGE_VALUES[1], EDGE_VALUES[2]};
    for (int ti = 0; ti < n_targets; ti++) {
        target_t* t = &targets[ti];
        if (t->narg != 2) {
            continue;
        }
        for (int a = 0; a < n_pool; a++) {
            for (int b = 0; b < n_pool; b++) {
                args[0] = pool[a];
                args[1] = pool[b];
                call(db, t, args, 2, rows);
            }
        }
    }
}

static void random_pass(sqlite3* db, double seconds) {
    static unsigned char bufs[MAX_ARGS + 2][8192];
    value_t args[MAX_ARGS], rows[2];
    time_t end = time(NULL) + (time_t)seconds;
    while (time(NULL) < end) {
        for (int k = 0; k < 1000; k++) {
            target_t* t = &targets[rnd_below(n_targets)];
            int counts[5];
            int n_counts = arg_counts(t, counts);
            if (n_counts == 0) {
                continue;
            }
            int n = counts[rnd_below(n_counts)];
            for (int j = 0; j < n; j++) {
                args[j] = random_value(bufs[j]);
            }
            rows[0] = random_value(bufs[MAX_ARGS]);
            rows[1] = random_value(bufs[MAX_ARGS + 1]);
            call(db, t, args, n, rows);
        }
    }
}

int main(int argc, char** argv) {
    double seconds = argc > 1 ? atof(argv[1]) : 60;
    unsigned long long seed = argc > 2 ? strtoull(argv[2], NULL, 10) : (unsigned long long)time(NULL);
    rng_state = seed ? seed : 1;
    repro_path = getenv("SQLEAN_FUZZ_REPRO") ? getenv("SQLEAN_FUZZ_REPRO") : "sqlean-fuzz-repro.sql";

    __sanitizer_set_death_callback(on_sanitizer_death);
    signal(SIGALRM, on_alarm);
    make_long_values();

    sqlite3 *plain, *db;
    sqlite3_open(":memory:", &plain);
    sqlite3_auto_extension((void (*)(void))sqlite3_sqlean_init);
    sqlite3_open(":memory:", &db);
    discover(plain, db);
    sqlite3_close(plain);

    printf("seed %llu, %d targets:", seed, n_targets);
    for (int j = 0; j < n_targets; j++) {
        printf(" %s/%d", targets[j].name, targets[j].narg);
    }
    printf("\n");
    fflush(stdout);

    edge_pass(db);
    printf("edge pass: %ld statements (%ld returned an error)\n", n_statements, n_errors);
    fflush(stdout);

    long done = n_statements;
    pair_pass(db);
    printf("pair pass: %ld statements\n", n_statements - done);
    fflush(stdout);

    done = n_statements;
    random_pass(db, seconds);
    printf("random pass: %ld statements in %.0f s\n", n_statements - done, seconds);
    printf("no sanitizer reports, no hangs\n");
    sqlite3_close(db);
    return 0;
}
