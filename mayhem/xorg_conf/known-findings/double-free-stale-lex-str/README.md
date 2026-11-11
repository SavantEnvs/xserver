# xorg.conf parser: double free of the current token string on a bad keyword

`input.conf`: a `Secti\xNN\xNN` keyword inside a `ServerFlags` section. The option reader keeps the token
string it just scanned (`xf86_lex_val.str`) as an option name/value; when the next keyword is invalid,
`xf86readConfigFile` (`hw/xfree86/parser/read.c:203`) frees `xf86_lex_val.str` again, and
`xf86freeConfig` then frees the same block via `xf86optionListFree` (`Flags.c:260`).
ASan: double-free. Impact: heap corruption from an attacker-supplied xorg.conf / `xorg.conf.d` snippet.
Fix: clear `xf86_lex_val.str` where the section reader hands it to the option list (or do not free it in
the `default:` / invalid-section branches of `xf86readConfigFile`).
The same bug makes `./xorg_conf -runs=100 mayhem/xorg_conf/testsuite` crash within a few seeds of mutation.
