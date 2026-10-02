-- Checks on top of sqlean's own suite (text.sql), same format.

-- '%' matches an empty sequence (patch 0004)
select 'x1_01', text_like('%abc', 'abc') = 1;
select 'x1_02', text_like('%abc%', 'abc') = 1;
select 'x1_03', text_like('a%b', 'ab') = 1;
select 'x1_04', text_like('%%a', 'a') = 1;
select 'x1_05', text_like('%', '') = 1;
select 'x1_06', text_like('%b%b', 'abab') = 1;
select 'x1_07', text_like('%x%', 'abc') = 0;
select 'x1_08', text_like('a%', '') = 0;

-- Cyrillic, case-insensitive
select 'x2_01', text_like('%ПРИВЕТ%', 'Привет мир') = 1;
select 'x2_02', text_like('%мир%', 'ПРИВЕТ МИР') = 1;
select 'x2_03', text_like('%ёж%', 'ЁЖ') = 1;
select 'x2_04', text_like('%е%', 'ё') = 0;
select 'x2_05', text_lower('ПРИВЕТ МИР ЁЖ') = 'привет мир ёж';
select 'x2_06', text_upper('привет мир ёж') = 'ПРИВЕТ МИР ЁЖ';
select 'x2_07', 'Ёлка' = 'ёЛКА' collate text_nocase;
select 'x2_08', text_lower('Привет мир') like '%' || text_lower('ПРИВЕТ') || '%';

-- Case conversions that change the UTF-8 width used to crash sqld (sqlean #164, patch 0003)
select 'x3_01', text_casefold('ẞ') is not null;
select 'x3_02', text_casefold('Straße ẞ') is not null;
select 'x3_03', text_lower('ẞ') = 'ß';
