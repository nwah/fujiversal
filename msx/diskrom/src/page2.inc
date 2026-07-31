;--- Page 2 of the 32K ROM, 8000h-BFFFh.
;
;    Every module that belongs in page 2 includes this. The section origin is
;    set once, in page2.s; declaring it twice is an error. appmake writes the
;    BANK_01 section at ROM offset 4000h, which is address 8000h once the
;    cartridge is mapped, so the name has to stay exactly this.

	SECTION	BANK_01
