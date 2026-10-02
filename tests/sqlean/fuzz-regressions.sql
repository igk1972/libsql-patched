-- Regressions for bugs found by fuzz:sqlean whose effect shows in SQL: a wrong result or a
-- statement that never finishes. Memory errors are covered by the fuzzer itself.

-- generate_series stops at the int64 range instead of overflowing and looping forever
select 'f1_01', (select count(*) from generate_series(1, 10)) = 10;
select 'f1_02', (select sum(value) from generate_series(1, 10, 3)) = 22;
select 'f1_03', (select group_concat(value) from generate_series(1, 10, -3)) = '10,7,4,1';
select 'f1_04', (select count(*) from generate_series(9223372036854775800, 9223372036854775807, 5)) = 2;
select 'f1_05', (select count(*) from generate_series(-9223372036854775808, -9223372036854775800, -5)) = 2;
select 'f1_06', (select count(*) from generate_series(0, 9223372036854775807, 9223372036854775807)) = 2;
select 'f1_07', (select count(*) from generate_series(-9223372036854775808, 9223372036854775807, -9223372036854775808)) = 3;

-- repeat of an empty string returns at once
select 'f2_01', text_repeat('', 2147483647) = '';
select 'f2_02', text_repeat('ab', 3) = 'ababab';

-- right/substring with INT_MIN
select 'f3_01', right('abc', -2147483648) = '';
select 'f3_02', right('abc', -1) = 'bc';
select 'f3_03', right('abc', -5) = '';
select 'f3_04', right('abc', 5) = 'abc';
select 'f3_05', text_substring('abc', -2147483648, 5) = '';
select 'f3_06', text_substring('abc', 2, 5) = 'bc';
