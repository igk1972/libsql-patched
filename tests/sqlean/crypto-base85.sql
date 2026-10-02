-- The base85 checks of test/crypto.sql in sqlean 0.28.4 (Copyright (c) 2021 Anton Zhiyanov, MIT
-- License, https://github.com/nalgeon/sqlean): the bundled crypto extension is older than 0.28.4,
-- only its base85.c is updated (patch 0005).

select '14_01', crypto_encode(null, 'base85') is null;
select '14_02', crypto_encode('', 'base85') = '';
select '14_03', crypto_encode('hell', 'base85') = 'BOu!r';
select '14_04', crypto_encode('hello', 'base85') = 'BOu!rDZ';
select '14_05', crypto_encode('hellow', 'base85') = 'BOu!rDg)';
select '14_06', crypto_encode('hellowo', 'base85') = 'BOu!rDg-+';
select '14_07', crypto_encode('эй, мир!', 'base85') = 'd>cM`/0N<Pd(%JR+T';
select '14_08', crypto_encode('(ಠ_ಠ)', 'base85') = '.,_D(?f85_.0';
select '14_09', crypto_encode('The quick brown 🦊 jumps over 13 lazy 🐶.', 'base85') = '<+ohcEHPu*CER),Dg-(An=QS8+DQ%9E-!.?G%G\:0f''qg@=!2An=PfN/c';
select '14_10', crypto_encode(x'00000000', 'base85') = 'z';
select '14_11', crypto_encode(x'0000000000000000', 'base85') = 'zz';
select '14_12', crypto_encode(x'00', 'base85') = '!!';
select '14_13', crypto_encode(x'000000', 'base85') = '!!!!';
select '14_14', crypto_encode(x'0000000000', 'base85') = 'z!!';
select '14_15', crypto_encode(x'000000006162636400000000', 'base85') = 'z@:E_Wz';
select '14_16', crypto_encode(x'ffffffff', 'base85') = 's8W-!';
select '14_17', crypto_encode(x'ffffff', 'base85') = 's8W*';
select '15_01', crypto_decode(null, 'base85') is null;
select '15_02', crypto_decode('', 'base85') = cast('' as blob);
select '15_03', crypto_decode('BOu!r', 'base85') = cast('hell' as blob);
select '15_04', crypto_decode('BOu!rDZ', 'base85') = cast('hello' as blob);
select '15_05', crypto_decode('BOu!rDg)', 'base85') = cast('hellow' as blob);
select '15_06', crypto_decode('BOu!rDg-+', 'base85') = cast('hellowo' as blob);
select '15_07', crypto_decode('d>cM`/0N<Pd(%JR+T', 'base85') = cast('эй, мир!' as blob);
select '15_08', crypto_decode('.,_D(?f85_.0', 'base85') = cast('(ಠ_ಠ)' as blob);
select '15_09', crypto_decode('<+ohcEHPu*CER),Dg-(An=QS8+DQ%9E-!.?G%G\:0f''qg@=!2An=PfN/c', 'base85') = cast('The quick brown 🦊 jumps over 13 lazy 🐶.' as blob);
select '15_10', crypto_decode('z', 'base85') = x'00000000';
select '15_11', crypto_decode('zz', 'base85') = x'0000000000000000';
select '15_12', crypto_decode('z!!', 'base85') = x'0000000000';
select '15_13', crypto_decode('z@:E_Wz', 'base85') = x'000000006162636400000000';
select '15_14', crypto_decode('!!!!!', 'base85') = x'00000000';
select '15_15', crypto_decode('!!', 'base85') = x'00';
select '15_16', crypto_decode('@/', 'base85') = cast('a' as blob);
select '15_17', crypto_decode('@:B', 'base85') = cast('ab' as blob);
select '15_18', crypto_decode('@:E^', 'base85') = cast('abc' as blob);
select '15_19', crypto_decode('@:E_W', 'base85') = cast('abcd' as blob);
select '15_20', crypto_decode('@:E_WAH', 'base85') = cast('abcde' as blob);
select '15_21', crypto_decode('F*2M7/c', 'base85') = cast('sure.' as blob);
select '15_22', crypto_decode('s8W-!', 'base85') = x'ffffffff';
select '15_23', crypto_decode('s8W*', 'base85') = x'ffffff';
select '15_24', crypto_decode(crypto_encode(x'0000000000', 'base85'), 'base85') = x'0000000000';
select '15_25', crypto_decode(crypto_encode('The quick brown fox', 'base85'), 'base85') = cast('The quick brown fox' as blob);
