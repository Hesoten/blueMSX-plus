;------------------------------------------------------------------------------
; BIOSDUMP.COM - dumps the ROMs a blueMSX+ machine config.ini lists
;
; Copyright (C) 2026 Hesoten
; See https://github.com/Hesoten/blueMSX-plus for change history.
;
; Redistribution and use in source and binary forms, with or without
; modification, are permitted provided that the following conditions are met:
;
; 1. Redistributions of source code must retain the above copyright notice,
;    this list of conditions and the following disclaimer.
;
; 2. Redistributions in binary form must reproduce the above copyright notice,
;    this list of conditions and the following disclaimer in the documentation
;    and/or other materials provided with the distribution.
;
; 3. Neither the name of the copyright holder nor the names of its
;    contributors may be used to endorse or promote products derived from
;    this software without specific prior written permission.
;
; THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
; AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
; IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
; ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
; LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
; CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
; SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
; INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
; CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
; ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
; POSSIBILITY OF SUCH DAMAGE.
;------------------------------------------------------------------------------
; Runs under MSX-DOS 1, MSX-DOS 2 and Nextor on a 64 KB MSX with a disk drive,
; on the Z80 and the R800. Assemble with sjasmplus: sjasmplus BIOSDUMP.ASM
;
; Usage: BIOSDUMP [config file] [d:] [/Y] [/S]
;   config file  default CONFIG.INI in the current directory
;   d:           drive the ROM files are written to (default: current drive)
;   /Y           do not ask anything before starting
;   /S           always write 128 KB parts (NAME.000, NAME.001, ...)
;------------------------------------------------------------------------------

        OUTPUT  "BIOSDUMP.COM"

BDOS    EQU     0005h
RDSLT   EQU     000Ch
CALSLT  EQU     001Ch
ENASLT  EQU     0024h
TPATOP  EQU     0006h
CMDLINE EQU     0080h
RAMAD1  EQU     0F342h
RAMAD2  EQU     0F343h
EXPTBL  EQU     0FCC1h
CHGCPU  EQU     0180h
GETCPU  EQU     0183h
JIFFY   EQU     0FC9Eh

; Data from a ROM switched into page 1 goes to BUFB, everything else to BUFA.
BUFA    EQU     4000h
BUFB    EQU     8000h

M_LIN   EQU     0
M_KANJI EQU     1
M_K12   EQU     2
M_PANA  EQU     3
M_A1FM  EQU     4
M_MODEM EQU     5
M_HAL   EQU     6
M_TC    EQU     7
M_NOMAP EQU     8
M_UNSUP EQU     9
M_DUP   EQU     10

E_SLOT  EQU     0
E_PRIM  EQU     1
E_SUB   EQU     2
E_PAGE  EQU     3
E_CNT   EQU     4
E_TYPE  EQU     5
E_METH  EQU     6
E_TRUNC EQU     7
E_UNITS EQU     8
E_NAME  EQU     10
E_ORIG  EQU     21
E_CUT   EQU     48
ENTSZ   EQU     50
MAXENT  EQU     32
ORIGMAX EQU     26

        ORG     0100h

start:  ld      hl,(TPATOP)
        ld      a,h
        cp      0C2h
        jr      nc,.tpaok
        ld      hl,m_tpa
        call    puts
        ld      c,0
        jp      BDOS
.tpaok: ld      sp,hl
        ld      hl,m_title
        call    puts
        call    dos_init
        call    parse_cmd
        call    ask_input
        call    get_free
        jr      nc,.drvok
        ld      hl,m_baddrv
        call    puts
        jp      exit
.drvok: ld      a,(quiet)
        or      a
        jr      z,.noq
        ld      a,(f_yes)
        or      a
        jr      nz,.noq
        ; a question would go into the > file and wait unseen
        ld      hl,m_needy
        call    puts
        jp      exit
.noq:   call    load_config
        call    parse_config
        call    dedupe
        call    uniquify
        ld      a,(n_ent)
        or      a
        jr      nz,.have
        ld      hl,m_noent
        call    puts
        jp      exit
.have:  ld      a,(n_over)
        or      a
        jr      z,.nov
        ld      hl,m_over
        call    puts
.nov:   call    show_plan
        call    confirm
        ld      a,(is_dos2)
        or      a
        jr      z,.nab
        ld      de,abort_rt
        ld      c,63h
        call    BDOS
.nab:   call    cpu_enter
        call    dump_all
        call    cpu_leave
        call    summary
exit:   call    cpu_leave
        call    k12_restore
        call    pana_restore
        ; a disk reset after a swap selects A:, so put the current drive back
        ld      a,(cur_drv)
        ld      e,a
        ld      c,0Eh
        call    BDOS
        ld      a,(is_dos2)
        or      a
        jr      z,.end
        ld      de,0
        ld      c,63h
        call    BDOS
.end:   ld      c,0
        jp      BDOS

;------------------------------------------------------------------------------
; Start-up
;------------------------------------------------------------------------------

dos_init:
        ld      c,19h
        call    BDOS
        ld      (cur_drv),a
        ld      b,0
        ld      c,6Fh
        call    BDOS
        or      a
        ld      a,0
        jr      nz,.d1
        ld      a,b
        cp      2
        ld      a,0
        jr      c,.d1
        inc     a
.d1:    ld      (is_dos2),a
        or      a
        jr      z,.ver
        ; progress lines only go to a screen, not into a > file
        xor     a
        ld      b,1
        ld      c,4Bh
        call    BDOS
        or      a
        jr      nz,.ver
        bit     7,e
        jr      nz,.ver
        ld      a,1
        ld      (quiet),a
.ver:   ld      a,(EXPTBL)
        ld      hl,002Dh
        call    RDSLT
        ei
        ld      (msxver),a
        ret

parse_cmd:
        ld      hl,CMDLINE
        ld      b,(hl)
        inc     hl
.tok:   call    .skip
        ret     z
        ld      a,(hl)
        cp      '/'
        jr      nz,.word
        inc     hl
        dec     b
        ret     z
        ld      a,(hl)
        call    upcase
        cp      'Y'
        jr      nz,.o2
        ld      (f_yes),a
.o2:    cp      'S'
        jr      nz,.o3
        ld      (f_split),a
.o3:    cp      '?'
        jr      z,.help
        cp      'H'
        jr      z,.help
.rest:  ld      a,b
        or      a
        ret     z
        ld      a,(hl)
        cp      ' '+1
        jr      c,.tok
        inc     hl
        dec     b
        jr      .rest
.help:  ld      hl,m_usage
        call    puts
        jp      exit
.word:  ld      de,tokbuf
        ld      c,0
.wl:    ld      a,b
        or      a
        jr      z,.wend
        ld      a,(hl)
        cp      ' '+1
        jr      c,.wend
        call    upcase
        ld      (de),a
        inc     de
        inc     hl
        dec     b
        inc     c
        ld      a,c
        cp      30
        jr      c,.wl
.wend:  xor     a
        ld      (de),a
        ld      a,c
        cp      2
        jr      nz,.name
        ld      a,(tokbuf+1)
        cp      ':'
        jr      nz,.name
        ld      a,(tokbuf)
        sub     'A'-1
        jr      z,.name
        cp      9
        jr      nc,.name
        ld      (t_drive),a
        ld      (drv_given),a
        jp      .tok
.name:  push    hl
        push    bc
        ld      hl,tokbuf
        ld      de,cfgname
        ld      bc,31
        ldir
        pop     bc
        pop     hl
        ld      a,1
        ld      (cfg_given),a
        jp      .tok
; Skips blanks; Z when the command line is used up.
.skip:  ld      a,b
        or      a
        ret     z
        ld      a,(hl)
        cp      ' '+1
        jr      nc,.nz
        inc     hl
        dec     b
        jr      .skip
.nz:    or      a
        ret

ask_input:
        ld      a,(cfg_given)
        or      a
        jr      nz,.drv
        call    noask
        jr      nz,.drv
        ld      hl,m_askcfg
        call    puts
        call    readline
        ld      a,(lbuf+1)
        or      a
        jr      z,.drv
        ld      hl,lbuf+2
        ld      de,cfgname
        ld      b,a
.cp:    ld      a,(hl)
        call    upcase
        ld      (de),a
        inc     hl
        inc     de
        djnz    .cp
        xor     a
        ld      (de),a
.drv:   ld      a,(drv_given)
        or      a
        jr      nz,.fcb
        call    noask
        jr      nz,.fcb
        ld      hl,m_askdrv
        call    puts
        call    readline
        ld      a,(lbuf+1)
        or      a
        jr      z,.fcb
        ld      a,(lbuf+2)
        call    upcase
        cp      'A'
        jr      c,.fcb
        cp      'H'+1
        jr      nc,.fcb
        sub     'A'-1
        ld      (t_drive),a
.fcb:   ld      a,(t_drive)
        or      a
        jr      nz,.letter
        ld      a,(cur_drv)
        inc     a
        ld      (t_drive),a
.letter:
        add     a,'A'-1
        ld      (drvletter),a
        ld      hl,cfgname
        ld      de,fcb_cfg
        jp      name_to_fcb

; HL = "[d:]NAME.EXT", DE = FCB (37 bytes) to fill.
name_to_fcb:
        push    hl
        push    de
        ld      h,d
        ld      l,e
        ld      (hl),0
        inc     de
        ld      bc,36
        ldir
        pop     de
        push    de
        inc     de
        ld      h,d
        ld      l,e
        ld      (hl),' '
        inc     de
        ld      bc,10
        ldir
        pop     de
        pop     hl
        inc     hl
        ld      a,(hl)
        dec     hl
        cp      ':'
        jr      nz,.nod
        ld      a,(hl)
        call    upcase
        sub     'A'-1
        ld      (de),a
        inc     hl
        inc     hl
.nod:   inc     de
        ld      b,8
.base:  ld      a,(hl)
        or      a
        ret     z
        inc     hl
        cp      '.'
        jr      z,.ext
        dec     b
        inc     b
        jr      z,.base
        call    upcase
        ld      (de),a
        inc     de
        dec     b
        jr      .base
.ext:   ld      a,b
        add     a,e
        ld      e,a
        jr      nc,.e1
        inc     d
.e1:    ld      b,3
.el:    ld      a,(hl)
        or      a
        ret     z
        call    upcase
        ld      (de),a
        inc     hl
        inc     de
        djnz    .el
        ret

load_config:
        ld      de,fcb_cfg
        ld      c,0Fh
        call    BDOS
        or      a
        jr      z,.open
        ld      hl,m_nocfg
        call    puts
        ld      hl,cfgname
        call    puts
        call    crlf
        jp      exit
.open:  ld      hl,(fcb_cfg+16)
        ld      a,(fcb_cfg+18)
        ld      b,a
        ld      a,(fcb_cfg+19)
        or      b
        jr      nz,.big
        ld      a,h
        cp      3Fh
        jr      c,.sz
.big:   ld      hl,3F00h
.sz:    ld      (cfg_size),hl
        ld      hl,BUFA
.rd:    push    hl
        ex      de,hl
        ld      c,1Ah
        call    BDOS
        ld      de,fcb_cfg
        ld      c,14h
        call    BDOS
        pop     hl
        or      a
        jr      nz,.eof
        ld      de,128
        add     hl,de
        ld      a,h
        cp      7Fh
        jr      c,.rd
.eof:   ld      de,fcb_cfg
        ld      c,10h
        call    BDOS
        ld      hl,(cfg_size)
        ld      de,BUFA
        add     hl,de
        ld      (hl),0
        ret

;------------------------------------------------------------------------------
; config.ini parsing
;------------------------------------------------------------------------------

parse_config:
        ld      hl,BUFA
        xor     a
        ld      (insec),a
        ld      (n_ent),a
        ld      ix,enttbl
.line:  ld      a,(hl)
        or      a
        ret     z
        cp      1Ah
        ret     z
        call    skip_ws
        ld      a,(hl)
        cp      '['
        jr      nz,.nsec
        call    is_slots
        ld      a,0
        jr      nz,.set
        inc     a
.set:   ld      (insec),a
        jr      .eol
.nsec:  ld      a,(insec)
        or      a
        jr      z,.eol
        ld      a,(hl)
        call    isdigit
        jr      nc,.eol
        push    hl
        call    parse_slot
        pop     hl
.eol:   ld      a,(hl)
        or      a
        ret     z
        cp      1Ah
        ret     z
        inc     hl
        cp      0Ah
        jr      nz,.eol
        jr      .line

skip_ws:
        ld      a,(hl)
        cp      ' '
        jr      z,.n
        cp      9
        ret     nz
.n:     inc     hl
        jr      skip_ws

; Z when HL points at "[Slots]" in any case. Keeps HL.
is_slots:
        push    hl
        ld      de,s_slots
.c:     ld      a,(de)
        or      a
        jr      z,.yes
        ld      b,a
        ld      a,(hl)
        call    upcase
        cp      b
        jr      nz,.no
        inc     hl
        inc     de
        jr      .c
.yes:   pop     hl
        ret
.no:    pop     hl
        or      1
        ret

; HL at the first digit of "<slot> <sub> <page> <count> <type> "<path>" ..."
parse_slot:
        ld      de,nums
        ld      b,5
.num:   call    skip_ws
        ld      a,(hl)
        call    isdigit
        ret     nc
        push    bc
        push    de
        call    read_num
        ex      (sp),hl
        ld      (hl),e
        inc     hl
        ld      (hl),d
        inc     hl
        ex      (sp),hl
        pop     de
        pop     bc
        djnz    .num
.q1:    ld      a,(hl)
        call    is_eol
        ret     z
        inc     hl
        cp      '"'
        jr      nz,.q1
        ld      (p_start),hl
        ld      (b_start),hl
.q2:    ld      a,(hl)
        call    is_eol
        ret     z
        cp      '"'
        jr      z,.q2e
        cp      5Ch
        jr      z,.sep
        cp      '/'
        jr      nz,.q2n
.sep:   inc     hl
        ld      (b_start),hl
        jr      .q2
.q2n:   inc     hl
        jr      .q2
.q2e:   ld      (p_end),hl
        ld      de,(b_start)
        or      a
        sbc     hl,de
        ret     z
        ; last dot of the basename
        ld      hl,(p_end)
        ld      (p_dot),hl
        ld      hl,(b_start)
.dot:   ld      de,(p_end)
        call    cmp_hl_de
        jr      z,.dote
        ld      a,(hl)
        cp      '.'
        jr      nz,.dn
        ld      (p_dot),hl
.dn:    inc     hl
        jr      .dot
.dote:  ld      a,(n_ent)
        cp      MAXENT
        jr      c,.room
        ld      a,1
        ld      (n_over),a
        ret
.room:  ; fill the entry
        push    ix
        pop     de
        ld      h,d
        ld      l,e
        ld      (hl),0
        inc     de
        ld      bc,ENTSZ-1
        ldir
        ld      a,(nums)
        and     3
        ld      (ix+E_PRIM),a
        ld      a,(nums+2)
        and     3
        ld      (ix+E_SUB),a
        ld      a,(nums+4)
        ld      (ix+E_PAGE),a
        ld      a,(nums+6)
        ld      (ix+E_CNT),a
        ld      a,(nums+9)
        or      a
        ld      a,255
        jr      nz,.t
        ld      a,(nums+8)
.t:     ld      (ix+E_TYPE),a
        ld      e,(ix+E_PRIM)
        ld      d,0
        ld      hl,EXPTBL
        add     hl,de
        ld      a,(hl)
        and     80h
        ld      a,e
        jr      z,.ne
        ld      a,(ix+E_SUB)
        add     a,a
        add     a,a
        or      e
        or      80h
.ne:    ld      (ix+E_SLOT),a
        ; FCB name: base up to 8, extension up to 3
        push    ix
        pop     hl
        ld      de,E_NAME
        add     hl,de
        ld      d,h
        ld      e,l
        ld      b,11
.sp:    ld      (hl),' '
        inc     hl
        djnz    .sp
        ld      hl,(b_start)
        ld      b,8
.bl:    push    de
        ld      de,(p_dot)
        call    cmp_hl_de
        pop     de
        jr      z,.be
        ld      a,b
        or      a
        jr      nz,.bc
        ld      (ix+E_TRUNC),1
        inc     hl
        jr      .bl
.bc:    ld      a,(hl)
        call    fcbchar
        ld      (de),a
        inc     hl
        inc     de
        dec     b
        jr      .bl
.be:    push    ix
        pop     de
        ld      hl,E_NAME+8
        add     hl,de
        ex      de,hl
        ld      hl,(p_dot)
        ld      bc,(p_end)
        call    cmp_hl_bc
        jr      z,.oe
        inc     hl
        ld      b,4
.xl:    push    bc
        ld      bc,(p_end)
        call    cmp_hl_bc
        pop     bc
        jr      z,.oe
        dec     b
        jr      nz,.xc
        ld      (ix+E_TRUNC),1
        jr      .oe
.xc:    ld      a,(hl)
        call    fcbchar
        ld      (de),a
        inc     hl
        inc     de
        jr      .xl
.oe:    ld      a,(ix+E_NAME)
        cp      ' '
        jr      nz,.on
        ld      (ix+E_NAME),'_'
        ld      (ix+E_TRUNC),1
.on:    ; original basename, for the rename hint
        push    ix
        pop     de
        ld      hl,E_ORIG
        add     hl,de
        ex      de,hl
        ld      hl,(b_start)
        ld      b,ORIGMAX
.ol:    push    bc
        ld      bc,(p_end)
        call    cmp_hl_bc
        pop     bc
        jr      z,.od
        ld      a,(hl)
        call    upcase
        ld      (de),a
        inc     hl
        inc     de
        djnz    .ol
        ld      bc,(p_end)
        call    cmp_hl_bc
        jr      z,.od
        ld      (ix+E_CUT),1
        ld      a,1
        ld      (n_over),a
.od:    xor     a
        ld      (de),a
        call    assign
        ld      de,ENTSZ
        add     ix,de
        ld      hl,n_ent
        inc     (hl)
        ret

; Sets E_METH and E_UNITS (8 KB units, nominal) from the type.
assign: ld      a,(ix+E_TYPE)
        ld      hl,typetab
.lp:    ld      b,(hl)
        inc     hl
        ld      c,(hl)
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        inc     hl
        inc     b
        jr      z,.def
        dec     b
        cp      b
        jr      nz,.lp
        jr      .got
.def:   ld      c,M_LIN
        ld      de,0
.got:   ld      (ix+E_METH),c
        ld      (ix+E_UNITS),e
        ld      (ix+E_UNITS+1),d
        ld      a,c
        cp      M_LIN
        jr      nz,.lay
        ld      a,(ix+E_CNT)
        or      a
        jr      nz,.l1
        ld      (ix+E_METH),M_NOMAP
        ret
.l1:    ld      b,a
        ld      a,e
        or      a
        jr      z,.l2
        cp      b
        jr      nc,.l2
        ld      b,a
.l2:    ld      (ix+E_UNITS),b
        ld      (ix+E_UNITS+1),0
        ld      a,b
        cp      9
        jr      nc,.bad
        ld      a,(ix+E_PAGE)
        cp      8
        jr      nc,.bad
        add     a,b
        cp      9
        ret     c
        jr      .bad
.lay:   ; the mappers below only exist at one start page
        ld      b,0
        cp      M_PANA
        jr      z,.pg
        cp      M_A1FM
        jr      z,.pg
        cp      M_HAL
        jr      z,.pg
        ld      b,2
        cp      M_MODEM
        jr      z,.pg
        cp      M_TC
        ret     nz
.pg:    ld      a,(ix+E_PAGE)
        cp      b
        ret     z
.bad:   ld      (ix+E_METH),M_UNSUP
        ret

; Of entries naming the same file only the biggest is dumped.
dedupe: ld      a,(n_ent)
        or      a
        ret     z
        ld      b,a
        ld      ix,enttbl
.i:     push    bc
        ld      a,(ix+E_METH)
        cp      M_NOMAP
        jr      nc,.inext
        push    ix
        pop     iy
        dec     b
        jr      z,.inext
.j:     ld      de,ENTSZ
        add     iy,de
        ld      a,(iy+E_METH)
        cp      M_NOMAP
        jr      nc,.jnext
        push    bc
        push    ix
        pop     hl
        ld      de,E_ORIG
        add     hl,de
        push    hl
        push    iy
        pop     hl
        add     hl,de
        pop     de
        ld      b,ORIGMAX+1
.cmp:   ld      a,(de)
        cp      (hl)
        jr      nz,.diff
        or      a
        jr      z,.same
        inc     hl
        inc     de
        djnz    .cmp
.same:  ; same file name: drop the smaller, the later one on a tie
        ld      l,(ix+E_UNITS)
        ld      h,(ix+E_UNITS+1)
        ld      e,(iy+E_UNITS)
        ld      d,(iy+E_UNITS+1)
        or      a
        sbc     hl,de
        jr      c,.dropi
        ld      (iy+E_METH),M_DUP
        jr      .diff
.dropi: ld      (ix+E_METH),M_DUP
        pop     bc
        jr      .inext
.diff:  pop     bc
.jnext: djnz    .j
.inext: ld      de,ENTSZ
        add     ix,de
        pop     bc
        djnz    .i
        ret

;------------------------------------------------------------------------------
; Plan and confirmation
;------------------------------------------------------------------------------

show_plan:
        ld      hl,m_plan
        call    puts
        ld      a,(n_ent)
        ld      b,a
        ld      ix,enttbl
.l:     push    bc
        call    print_entry
        ld      de,ENTSZ
        add     ix,de
        pop     bc
        djnz    .l
        ret

; One line: name, slot, type, method or reason, nominal size.
print_entry:
        ld      e,' '
        call    putc
        call    print_name
        ld      hl,m_slot
        call    puts
        ld      a,(ix+E_PRIM)
        add     a,'0'
        ld      e,a
        call    putc
        ld      a,(ix+E_SLOT)
        bit     7,a
        jr      z,.ns
        ld      e,'-'
        call    putc
        ld      a,(ix+E_SUB)
        add     a,'0'
        ld      e,a
        call    putc
.ns:    ld      hl,m_type
        call    puts
        ld      l,(ix+E_TYPE)
        ld      h,0
        call    print_dec
        ld      e,' '
        call    putc
        ld      a,(ix+E_METH)
        cp      M_NOMAP
        jr      nc,.skip
        call    meth_name
        call    puts
        ld      e,' '
        call    putc
        ld      l,(ix+E_UNITS)
        ld      h,(ix+E_UNITS+1)
        call    units_kb
        call    print_dec
        ld      hl,m_kb
        call    puts
        jp      crlf
.skip:  sub     M_NOMAP
        ld      hl,m_r_nomap
        jr      z,.r
        ld      hl,m_r_unsup
        dec     a
        jr      z,.r
        ld      hl,m_r_dup
.r:     call    puts
        jp      crlf

print_name:
        push    ix
        pop     hl
        ld      de,E_NAME
        add     hl,de
print_fcbname:
        ld      b,8
.b:     ld      a,(hl)
        inc     hl
        cp      ' '
        jr      z,.bs
        ld      e,a
        call    putc
.bs:    djnz    .b
        ld      a,(hl)
        cp      ' '
        ret     z
        ld      e,'.'
        call    putc
        ld      b,3
.e:     ld      a,(hl)
        inc     hl
        cp      ' '
        ret     z
        ld      e,a
        call    putc
        djnz    .e
        ret

; HL = units -> HL = KB
units_kb:
        add     hl,hl
        add     hl,hl
        add     hl,hl
        ret

; A = method -> HL = name
meth_name:
        add     a,a
        ld      e,a
        ld      d,0
        ld      hl,methnames
        add     hl,de
        ld      a,(hl)
        inc     hl
        ld      h,(hl)
        ld      l,a
        ret

confirm:
        ld      a,(f_yes)
        or      a
        ret     nz
        ld      a,(quiet)
        or      a
        jr      z,.ask
        ld      a,(is_dos2)
        or      a
        jr      nz,.ask
        ret
.ask:
        ld      a,(drvletter)
        ld      (m_pdrv),a
        ld      hl,m_proceed
        call    putp
.k:     call    getkey
        cp      'N'
        jr      z,.no
        cp      1Bh
        jr      z,.no
        cp      3
        jr      z,.no
        cp      'Y'
        jr      nz,.k
        ld      e,a
        call    putc
        jp      crlf
.no:    call    crlf
        jp      exit

;------------------------------------------------------------------------------
; turbo R: the ROM shadow of R800 DRAM mode is not the ROM, so read in ROM mode
;------------------------------------------------------------------------------

cpu_enter:
        ld      a,(msxver)
        cp      3
        ret     c
        ld      ix,GETCPU
        call    call_bios
        and     3
        ld      (cpu_orig),a
        cp      2
        ret     nz
        ld      a,81h
        ld      ix,CHGCPU
        call    call_bios
        ld      a,1
        ld      (cpu_sw),a
        ld      ix,GETCPU
        call    call_bios
        and     3
        ld      hl,m_cpusw
        cp      1
        jr      z,.p
        ld      hl,m_cpufail
.p:     jp      puts

; DOS 2 calls this before it aborts the program (Ctrl-C, disk error).
abort_rt:
        push    af
        push    bc
        ; once only: the output below can itself end in an abort
        ld      a,(ab_in)
        or      a
        jr      nz,.q
        inc     a
        ld      (ab_in),a
        ; MSX-DOS 2 calls this in a stable state that accepts any function
        ; call, with the TPA paged in (function spec, function 63h)
        call    cpu_leave
        call    k12_restore
        call    pana_restore
        ; the unfinished file goes, as when BIOSDUMP stops by itself
        ld      a,(w_open)
        or      a
        jr      z,.nf
        xor     a
        ld      (w_open),a
        ld      de,fcb_out
        ld      c,10h
        call    BDOS
        call    write_file.clrfcb
        ld      de,fcb_out
        ld      c,13h
        call    BDOS
        ld      a,1
        ld      (brk_file),a
.nf:    ld      a,(cur_drv)
        ld      e,a
        ld      c,0Eh
        call    BDOS
        ; messages only to a screen, through 06h, which does not check CTRL+C;
        ; with the output redirected the abort may come from that very file
        ld      a,(quiet)
        or      a
        jr      nz,.q
        ld      a,(brk_file)
        or      a
        ld      hl,m_brkf6
        call    nz,puts6
        ld      a,(w_inparts)
        or      a
        ld      hl,m_abinc
        call    nz,puts6
        ld      a,(any_noreg)
        or      a
        ld      hl,m_noreg
        call    nz,puts6
.q:     pop     bc
        pop     af
        ret

puts6:  ld      a,(hl)
        or      a
        ret     z
        push    hl
        ld      e,a
        ld      c,6
        call    BDOS
        pop     hl
        inc     hl
        jr      puts6
cpu_leave:
        ld      a,(cpu_sw)
        or      a
        ret     z
        xor     a
        ld      (cpu_sw),a
        ld      a,(cpu_orig)
        or      80h
        ld      ix,CHGCPU
        jp      call_bios

; IX = main ROM routine, A passed through.
call_bios:
        ld      iy,(EXPTBL-1)
        call    CALSLT
        ei
        ret

;------------------------------------------------------------------------------
; Dumping
;------------------------------------------------------------------------------

dump_all:
        ld      a,(n_ent)
        ld      b,a
        ld      ix,enttbl
.l:     push    bc
        push    ix
        call    dump_entry
        pop     ix
        ld      de,ENTSZ
        add     ix,de
        pop     bc
        djnz    .l
        ret

dump_entry:
        call    crlf
        call    print_entry
        ld      a,(ix+E_METH)
        cp      M_NOMAP
        jr      c,.go
        ld      hl,n_skip
        inc     (hl)
        ret
.go:    ld      (cur_meth),a
        ld      a,(ix+E_SLOT)
        ld      (cur_slot),a
        ld      a,(ix+E_PAGE)
        ld      (cur_page),a
        ld      a,(ix+E_TYPE)
        ld      (cur_type),a
        ld      l,(ix+E_UNITS)
        ld      h,(ix+E_UNITS+1)
        ld      (cur_units),hl
        push    ix
        pop     hl
        ld      de,E_NAME
        add     hl,de
        ld      de,cur_name
        ld      bc,11
        ldir
        ld      a,(ix+E_TRUNC)
        or      a
        jr      z,.nt
        cp      2
        jr      nz,.tr
        ld      hl,m_renamed
        call    puts
        call    print_name
        call    crlf
.tr:    ld      hl,m_trunc
        call    puts
        push    ix
        pop     hl
        ld      de,E_ORIG
        add     hl,de
        call    puts
        ld      a,(ix+E_CUT)
        or      a
        ld      hl,m_cut
        call    nz,puts
        call    crlf
.nt:    ld      hl,probetab
        call    dispatch
        jr      nc,.ok
        push    hl
        ld      hl,m_skipped
        call    puts
        pop     hl
        call    puts
        call    crlf
        ld      hl,n_skip
        inc     (hl)
        jr      .fin
.ok:    ld      hl,m_size
        call    puts
        ld      hl,(cur_units)
        call    units_kb
        call    print_dec
        ld      hl,m_kb
        call    puts
        call    crlf
        call    write_rom
.fin:   ld      hl,fintab
        jp      dispatch

; Calls table[cur_meth].
dispatch:
        ld      a,(cur_meth)
        add     a,a
        ld      e,a
        ld      d,0
        add     hl,de
        ld      a,(hl)
        inc     hl
        ld      h,(hl)
        ld      l,a
        jp      (hl)

; DE = unit -> HL = 8 KB of the file
read_unit:
        ld      (ru_u),de
        ld      hl,readtab
        jr      dispatch

nothing:
        or      a
        ret

;------------------------------------------------------------------------------
; Output: one file, or 128 KB parts when the disk is too small or /S is given
;------------------------------------------------------------------------------

write_rom:
        xor     a
        ld      (w_inparts),a
        ld      a,(f_split)
        or      a
        jr      nz,.parts
        call    get_free
        ld      de,(cur_units)
        call    fits
        jr      c,.parts
        xor     a
        call    clean_parts
        ld      hl,cur_name
        ld      de,fcb_out+1
        ld      bc,11
        ldir
        ld      hl,0
        ld      (w_u0),hl
        ld      hl,(cur_units)
        ld      (w_u1),hl
        call    write_file
        jr      c,.sfail
        ld      hl,n_done
        inc     (hl)
        ret
.sfail: ld      hl,m_wfail
        call    puts
.parts: call    parts_info
        ld      a,1
        ld      (w_fresh),a
        ld      (w_inparts),a
        ld      hl,0
        ld      (w_part),hl
.next:  ld      hl,(w_part)
        add     hl,hl
        add     hl,hl
        add     hl,hl
        add     hl,hl
        ld      (w_u0),hl
        ex      de,hl
        ld      hl,(cur_units)
        or      a
        sbc     hl,de
        jp      z,.done
        jp      c,.done
        ld      de,16
        call    cmp_hl_de
        jr      c,.last
        ex      de,hl
.last:  ex      de,hl
        ld      hl,(w_u0)
        add     hl,de
        ld      (w_u1),hl
        ld      (w_n),de
.retry: call    get_free
        jr      c,.swap
        ld      de,(w_n)
        call    fits
        jr      nc,.room
.swap:  call    ask_swap
        ret     c
        ld      a,1
        ld      (w_fresh),a
        jr      .retry
.room:  ld      a,(w_fresh)
        or      a
        jr      z,.nocl
        ld      a,1
        call    clean_parts
.nocl:
        xor     a
        ld      (w_fresh),a
        ld      hl,cur_name
        ld      de,fcb_out+1
        ld      bc,8
        ldir
        ld      a,(w_part+1)
        call    hexdig
        ld      (fcb_out+9),a
        ld      a,(w_part)
        rrca
        rrca
        rrca
        rrca
        call    hexdig
        ld      (fcb_out+10),a
        ld      a,(w_part)
        call    hexdig
        ld      (fcb_out+11),a
        call    write_file
        jr      nc,.ok
        ld      hl,m_pfail
        call    puts
        jr      .swap
.ok:    ld      hl,(w_part)
        inc     hl
        ld      (w_part),hl
        jp      .next
.done:  xor     a
        ld      (w_inparts),a
        ld      hl,n_done
        inc     (hl)
        ld      a,1
        ld      (any_parts),a
        ret

; CTRL+C taken by BIOSDUMP itself (DOS 1, or a key question): the file being
; written is already gone; say what is left and end as at the normal exit.
stop_break:
        ; wait (up to about 4 s) for the keys to be let go, then drop the
        ; repeats, which COMMAND would otherwise take as CTRL+C after the exit
        ld      hl,(JIFFY)
        ld      (sb_t),hl
.rel:   call    ctrlc_held
        jr      nz,.drain
        ld      hl,(JIFFY)
        ld      de,(sb_t)
        or      a
        sbc     hl,de
        ld      a,h
        or      a
        jr      nz,.drain
        ld      a,l
        cp      240
        jr      c,.rel
.drain: ld      e,0FFh
        ld      c,6
        call    BDOS
        or      a
        jr      nz,.drain
        ld      hl,m_break
        call    puts
        ld      a,(brk_file)
        or      a
        ld      hl,m_brkf
        call    nz,puts
        ld      a,(w_inparts)
        or      a
        call    nz,say_incomplete
        jp      exit
; Z when CTRL (row 6 bit 1) and C (row 3 bit 0) are both down.
ctrlc_held:
        di
        in      a,(0AAh)
        ld      d,a
        and     0F0h
        ld      b,a
        or      6
        out     (0AAh),a
        in      a,(0A9h)
        and     2
        ld      c,a
        ld      a,b
        or      3
        out     (0AAh),a
        in      a,(0A9h)
        and     1
        or      c
        ld      c,a
        ld      a,d
        out     (0AAh),a
        ei
        ld      a,c
        or      a
        ret
say_incomplete:
        ld      hl,m_aborted
        call    puts
        ld      hl,(w_part)
        call    print_dec
        ld      hl,m_aborted2
        jp      puts

parts_info:
        ld      hl,m_parts1
        call    puts
        ld      hl,(cur_units)
        ld      de,15
        add     hl,de
        ld      b,4
.d:     srl     h
        rr      l
        djnz    .d
        call    print_dec
        ld      hl,m_parts2
        call    puts
        ld      hl,(cur_units)
        call    units_kb
        call    print_dec
        ld      hl,m_parts3
        jp      puts

; Part files NAME.000-01F of an earlier run on this disk. A = 0 before a
; single file: only reported. A = 1 before part w_part: parts numbered from
; w_part up are deleted (join would take them), lower ones only reported.
clean_parts:
        ld      (cp_mode),a
        xor     a
        ld      (cp_kept),a
        ld      de,BUFB
        ld      c,1Ah
        call    BDOS
        call    .fcb
        ld      a,'?'
        ld      (fcb_srch+9),a
        ld      (fcb_srch+10),a
        ld      (fcb_srch+11),a
        xor     a
        ld      (cp_n),a
        ld      hl,BUFA
        ld      (cp_p),hl
        ld      de,fcb_srch
        ld      c,11h
.s:     call    BDOS
        inc     a
        jr      z,.list
        ld      hl,BUFB+9
        call    is_part
        jr      nz,.nx
        ld      a,(cp_n)
        cp      64
        jr      nc,.nx
        inc     a
        ld      (cp_n),a
        ld      hl,BUFB+1
        ld      de,(cp_p)
        ld      bc,11
        ldir
        ld      (cp_p),de
.nx:    ld      de,fcb_srch
        ld      c,12h
        jr      .s
.list:  ld      a,(cp_n)
        or      a
        ret     z
        ld      b,a
        ld      hl,BUFA
.d:     push    bc
        push    hl
        call    .fcb
        pop     hl
        ld      de,fcb_srch+1
        ld      bc,11
        ldir
        push    hl
        ld      hl,fcb_srch+9
        call    is_part
        ld      b,a
        ld      a,(cp_mode)
        or      a
        jr      z,.keep
        ld      a,(w_part)
        ld      c,a
        ld      a,b
        cp      c
        jr      c,.keep
        ld      de,fcb_srch
        ld      c,13h
        call    BDOS
        inc     a
        ld      hl,m_nodel
        jr      z,.say
        ld      hl,m_deleted
        jr      .say
.keep:  ld      a,(cp_kept)
        inc     a
        ld      (cp_kept),a
        cp      4
        jr      nc,.next
        ld      hl,m_keep
        ld      a,(cp_mode)
        or      a
        jr      nz,.say
        ld      hl,m_keep0
.say:   call    puts
        call    .fcb2
.next:
        pop     hl
        pop     bc
        djnz    .d
        ld      a,(cp_kept)
        sub     3
        ret     c
        ret     z
        ld      l,a
        ld      h,0
        push    hl
        ld      hl,m_kmore1
        call    puts
        pop     hl
        call    print_dec
        ld      hl,m_kmore2
        jp      puts
.fcb2:  call    .nm
        jp      crlf
.nm:    ld      hl,fcb_srch+1
        jp      print_fcbname
.fcb:   ld      hl,fcb_srch
        ld      de,fcb_srch+1
        ld      (hl),0
        ld      bc,36
        ldir
        ld      a,(t_drive)
        ld      (fcb_srch),a
        ld      hl,cur_name
        ld      de,fcb_srch+1
        ld      bc,8
        ldir
        ret

; HL = extension. Z and A = part number when it is 000-01F.
is_part:
        ld      a,(hl)
        cp      '0'
        ret     nz
        inc     hl
        ld      a,(hl)
        sub     '0'
        cp      2
        jr      nc,.no
        rlca
        rlca
        rlca
        rlca
        ld      b,a
        inc     hl
        ld      a,(hl)
        sub     '0'
        cp      10
        jr      c,.ok
        sub     'A'-'0'
        cp      6
        jr      nc,.no
        add     a,10
.ok:    or      b
        cp      a
        ret
.no:    or      1
        ret
; Z when the three characters at HL are hex digits
is_hex3:
        ld      b,3
.l:     ld      a,(hl)
        inc     hl
        sub     '0'
        cp      10
        jr      c,.ok
        sub     'A'-'0'
        cp      6
        jr      nc,.no
.ok:    djnz    .l
        xor     a
        ret
.no:    or      1
        ret

; Gives an entry whose 8-character base equals an earlier one's (after the
; 8.3 cut) a digit at the end, so that no file or part overwrites another.
uniquify:
        ld      a,(n_ent)
        or      a
        ret     z
        ld      b,a
        ld      ix,enttbl
.i:     push    bc
        ld      a,(ix+E_METH)
        cp      M_NOMAP
        jr      nc,.next
        ld      a,'0'
        ld      (uq_d),a
.again: call    .clash
        jr      nz,.next
        ld      a,(uq_d)
        cp      '0'
        ld      hl,(uq_pos)
        jr      nz,.put2
        ; base length: replace the 8th character or append after the last
        push    ix
        pop     hl
        ld      de,E_NAME+7
        add     hl,de
        ld      b,7
.f:     ld      a,(hl)
        cp      ' '
        jr      nz,.put
        dec     hl
        djnz    .f
.put:   ld      a,b
        cp      7
        jr      z,.put2
        inc     hl
.put2:  ld      (uq_pos),hl
        ld      a,(uq_d)
        inc     a
        cp      '9'+1
        jr      nz,.dg
        ld      a,'A'
.dg:    ld      (uq_d),a
        ld      (hl),a
        ld      (ix+E_TRUNC),2
        jr      .again
.next:  ld      de,ENTSZ
        add     ix,de
        pop     bc
        djnz    .i
        ret
; Z when an earlier writing entry has the same base as IX
.clash: ld      iy,enttbl
.c1:    push    ix
        pop     hl
        push    iy
        pop     de
        or      a
        sbc     hl,de
        jr      z,.none
        ld      a,(iy+E_METH)
        cp      M_NOMAP
        jr      nc,.c3
        push    ix
        pop     hl
        ld      de,E_NAME
        add     hl,de
        push    hl
        push    iy
        pop     hl
        add     hl,de
        pop     de
        ld      b,8
.c2:    ld      a,(de)
        cp      (hl)
        jr      nz,.c3
        inc     hl
        inc     de
        djnz    .c2
        xor     a
        ret
.c3:    ld      de,ENTSZ
        add     iy,de
        jr      .c1
.none:  or      1
        ret

noask:  ld      a,(f_yes)
        ld      b,a
        ld      a,(quiet)
        or      b
        ret

; Questions are only asked with the output on the screen (see main).
putp:   jp      puts

; A low nibble -> ASCII hex digit
hexdig: and     0Fh
        add     a,'0'
        cp      '9'+1
        ret     c
        add     a,7
        ret

ask_swap:
        ld      a,(quiet)
        or      a
        jr      z,.ask
        ld      hl,m_noswap
        call    puts
        jr      .esc
.ask:
        ld      a,(drvletter)
        ld      (m_sdrv),a
        ld      hl,m_swap1
        call    putp
        call    getkey
        push    af
        call    crlf
        pop     af
        cp      1Bh
        jr      z,.esc
        cp      3
        jp      z,stop_break
        ld      c,0Dh
        call    BDOS
        or      a
        ret
.esc:   call    say_incomplete
        xor     a
        ld      (w_inparts),a
        ld      hl,n_skip
        inc     (hl)
        scf
        ret

; fcb_out+1 holds the name; writes units w_u0..w_u1. CY on failure (file deleted).
write_file:
        ld      a,(t_drive)
        ld      (fcb_out),a
        ld      hl,fcb_out+12
        ld      de,fcb_out+13
        ld      (hl),0
        ld      bc,24
        ldir
        ld      hl,m_writing
        call    puts
        ld      hl,fcb_out+1
        call    print_fcbname
        call    crlf
        ld      de,fcb_out
        ld      c,13h
        call    BDOS
        call    .clrfcb
        ld      de,fcb_out
        ld      c,16h
        call    BDOS
        or      a
        scf
        ret     nz
        ld      a,1
        ld      (w_open),a
        ld      hl,1
        ld      (fcb_out+14),hl
        ld      hl,0
        ld      (fcb_out+33),hl
        ld      (fcb_out+35),hl
        ld      hl,(w_u0)
        ld      (w_u),hl
.u:     ld      hl,(w_u)
        ld      de,(w_u1)
        call    cmp_hl_de
        jr      nc,.close
        ex      de,hl
        call    read_unit
        ex      de,hl
        ld      c,1Ah
        call    BDOS
        ld      de,fcb_out
        ld      hl,2000h
        ld      c,26h
        call    BDOS
        or      a
        jr      nz,.fail
        ld      hl,(w_u)
        inc     hl
        ld      (w_u),hl
        call    progress
        ; CTRL+C is taken here, between two blocks: a key pressed during a
        ; disk write can be dropped by DOS, and DOS 1 has no abort hook
        ld      e,0FFh
        ld      c,6
        call    BDOS
        cp      3
        jr      z,.brk
        ; also CTRL and C held now, in case the key code never reached the buffer
        call    ctrlc_held
        jr      nz,.u
.brk:
        xor     a
        ld      (w_open),a
        ld      de,fcb_out
        ld      c,10h
        call    BDOS
        call    .clrfcb
        ld      de,fcb_out
        ld      c,13h
        call    BDOS
        ld      a,1
        ld      (brk_file),a
        jp      stop_break
.close: xor     a
        ld      (w_open),a
        ld      de,fcb_out
        ld      c,10h
        call    BDOS
        or      a
        jr      nz,.fail2
        call    crlf
        or      a
        ret
.fail:  xor     a
        ld      (w_open),a
        ld      de,fcb_out
        ld      c,10h
        call    BDOS
.fail2: call    crlf
        call    .clrfcb
        ld      de,fcb_out
        ld      c,13h
        call    BDOS
        scf
        ret
.clrfcb:
        ld      hl,fcb_out+12
        ld      de,fcb_out+13
        ld      (hl),0
        ld      bc,24
        ldir
        ret

; Prints "\r  done/total KB" every 32 KB and at the end of each file.
progress:
        ld      hl,(w_u)
        ld      de,(w_u1)
        call    cmp_hl_de
        jr      z,.p
        ld      a,(quiet)
        or      a
        ret     nz
        ld      a,l
        and     3
        ret     nz
.p:     ld      e,13
        call    putc
        ld      hl,(w_u)
        call    units_kb
        ld      a,6
        call    print_dec_w
        ld      e,'/'
        call    putc
        ld      hl,(cur_units)
        call    units_kb
        call    print_dec
        ld      hl,m_kb
        jp      puts

; Free space of the target drive in fr_clu (clusters) and fr_cls (sectors per cluster).
get_free:
        ld      a,(t_drive)
        ld      e,a
        ld      c,1Bh
        call    BDOS
        cp      0FFh
        jr      z,.err
        or      a
        jr      z,.err
        ld      (fr_clu),hl
        ld      l,a
        ld      h,0
        ld      (fr_cls),hl
        or      a
        ret
.err:   ld      hl,0
        ld      (fr_clu),hl
        inc     hl
        ld      (fr_cls),hl
        scf
        ret

; DE = units; CY when they do not fit in the free clusters.
fits:   ex      de,hl
        add     hl,hl
        add     hl,hl
        add     hl,hl
        add     hl,hl
        ld      de,(fr_cls)
        dec     de
        add     hl,de
        inc     de
        ld      c,0
        call    div24
        ld      de,(fr_clu)
        ex      de,hl
        jp      cmp_hl_de

;------------------------------------------------------------------------------
; Slot switching. Interrupts stay off until the RAM is back.
;------------------------------------------------------------------------------

sel_p1: di
        ld      a,(cur_slot)
        ld      h,40h
        jp      ENASLT

rst_p1: ld      a,(RAMAD1)
        ld      h,40h
        call    ENASLT
        ei
        ret

sel_p2: di
        ld      a,(cur_slot)
        ld      h,80h
        jp      ENASLT

rst_p2: ld      a,(RAMAD2)
        ld      h,80h
        call    ENASLT
        ei
        ret

; HL = slot address, DE = destination, BC = length (within one page)
copy_range:
        ld      a,b
        or      c
        ret     z
        ld      (cr_src),hl
        ld      (cr_dst),de
        ld      (cr_len),bc
        ld      a,h
        and     0C0h
        cp      40h
        jr      z,.p1
        cp      80h
        jr      z,.p2
        cp      0C0h
        jr      z,.p3
.rl:    ld      hl,(cr_src)
        ld      a,(cur_slot)
        call    RDSLT
        ld      hl,(cr_dst)
        ld      (hl),a
        inc     hl
        ld      (cr_dst),hl
        ld      hl,(cr_src)
        inc     hl
        ld      (cr_src),hl
        ld      hl,(cr_len)
        dec     hl
        ld      (cr_len),hl
        ld      a,h
        or      l
        jr      nz,.rl
        ei
        ret
.p1:    call    sel_p1
        call    .ldir
        jp      rst_p1
.p2:    call    sel_p2
        call    .ldir
        jp      rst_p2
.ldir:  ld      hl,(cr_src)
        ld      de,(cr_dst)
        ld      bc,(cr_len)
        ldir
        ret
; The RDSLT of DOS runs in page 3 and cannot switch it, so page 3 is switched
; here from page 0, with interrupts off and no stack use until it is back.
.p3:    di
        ld      a,(cur_slot)
        and     3
        rrca
        rrca
        ld      c,a
        in      a,(0A8h)
        ld      (p3_a8),a
        and     3Fh
        or      c
        out     (0A8h),a
        ld      a,(cur_slot)
        bit     7,a
        jr      z,.p3c
        and     0Ch
        rlca
        rlca
        rlca
        rlca
        ld      c,a
        ld      a,(0FFFFh)
        cpl
        ld      (p3_sub),a
        and     3Fh
        or      c
        ld      (0FFFFh),a
.p3c:   ld      hl,(cr_src)
        ld      de,(cr_dst)
        ld      bc,(cr_len)
        ldir
        ld      a,(cur_slot)
        bit     7,a
        jr      z,.p3r
        ld      a,(p3_sub)
        ld      (0FFFFh),a
        ; FFFFh of an expanded slot is its slot register, not ROM
        ld      a,h
        or      l
        jr      nz,.p3r
        dec     de
        ld      a,0FFh
        ld      (de),a
.p3r:   ld      a,(p3_a8)
        out     (0A8h),a
        ei
        ret

; HL = device memory, DE = buffer, BC = length. Z when equal.
cmp_mem:
        ld      a,(de)
        inc     de
        cpi
        ret     nz
        jp      pe,cmp_mem
        ret

; HL = buffer, BC = length. Z when every byte is FFh.
all_ff: ld      a,(hl)
        inc     a
        ret     nz
        inc     hl
        dec     bc
        ld      a,b
        or      c
        jr      nz,all_ff
        ret

fill_ff:
        ld      d,h
        ld      e,l
        inc     de
        ld      (hl),0FFh
        dec     bc
        ldir
        ret

;------------------------------------------------------------------------------
; Linear ROM, with the memory-mapped registers some types have left out
;------------------------------------------------------------------------------

probe_lin:
        xor     a
        ld      (hole_len),a
        ld      a,(cur_type)
        cp      78
        jr      nz,.go
        ld      a,(cur_page)
        cp      2
        jr      nz,.go
        ld      a,(cur_units)
        cp      2
        jr      c,.go
        ; MSX-MUSIC-WX: 7FF0h-7FFFh read its control register ORed with FCh, and
        ; 4000h-7FEFh read FFh while bit 0 is 1. Only FCh/FEh count as the
        ; register, so a plain ROM ending in 16 of them is dumped with FFh there
        call    sel_p1
        ld      hl,7FF0h
        ld      a,(hl)
        ld      b,16
.c:     cp      (hl)
        jr      nz,.nw
        inc     hl
        djnz    .c
        or      2
        cp      0FEh
.nw:    ld      a,(4000h)
        push    af
        call    rst_p1
        pop     af
        jr      nz,.nh
        ld      hl,7FF0h
        ld      (hole_abs),hl
        ld      hl,hole_len
        ld      (hl),10h
.nh:    inc     a
        ld      hl,m_nomus
        scf
        ret     z
        or      a
        ret
.go:    ld      a,(cur_type)
        ld      hl,holetab
.l:     ld      b,(hl)
        inc     hl
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        inc     hl
        ld      c,(hl)
        inc     hl
        inc     b
        jr      z,.none
        dec     b
        cp      b
        jr      nz,.l
        ld      a,c
        ld      (hole_len),a
        ld      a,(cur_page)
        rrca
        rrca
        rrca
        ld      h,a
        ld      l,0
        add     hl,de
        ld      (hole_abs),hl
.none:  or      a
        ret

rd_lin: ld      a,(ru_u)
        ld      b,a
        ld      a,(cur_page)
        add     a,b
        rrca
        rrca
        rrca
        ld      h,a
        ld      l,0
        ld      (lin_src),hl
        and     0C0h
        cp      40h
        ld      de,BUFA
        jr      nz,.d
        ld      de,BUFB
.d:     ld      (lin_dst),de
        ld      a,(hole_len)
        or      a
        jr      z,.whole
        ld      de,(lin_src)
        ld      hl,(hole_abs)
        or      a
        sbc     hl,de
        jr      c,.whole
        ld      a,h
        cp      20h
        jr      nc,.whole
        ld      (lin_pre),hl
        ld      b,h
        ld      c,l
        ld      hl,(lin_src)
        ld      de,(lin_dst)
        call    copy_range
        ld      hl,(lin_dst)
        ld      de,(lin_pre)
        add     hl,de
        ld      a,(hole_len)
        ld      b,a
.f:     ld      (hl),0FFh
        inc     hl
        djnz    .f
        ld      hl,(lin_pre)
        ld      a,(hole_len)
        ld      e,a
        ld      d,0
        add     hl,de
        ex      de,hl
        ld      hl,2000h
        or      a
        sbc     hl,de
        ld      b,h
        ld      c,l
        ld      hl,(lin_dst)
        add     hl,de
        push    hl
        ld      hl,(lin_src)
        add     hl,de
        pop     de
        call    copy_range
        jr      .ret
.whole: ld      hl,(lin_src)
        ld      de,(lin_dst)
        ld      bc,2000h
        call    copy_range
.ret:   ld      hl,(lin_dst)
        ret

;------------------------------------------------------------------------------
; Kanji ROM, I/O ports D8h-D9h (JIS level 1) and DAh-DBh (level 2)
;------------------------------------------------------------------------------

probe_kanji:
        ld      c,0D8h
        ld      d,0
        ld      e,1
        ld      hl,BUFA
        call    kanji_rows
        ld      hl,BUFA
        ld      bc,800h
        call    all_ff
        ld      hl,m_nokanji
        scf
        ret     z
        ; level 2 is judged on every 8th row, as a row of it can be blank
        xor     a
        ld      (k_row),a
.n:     ld      c,0D8h
        ld      d,a
        ld      e,1
        ld      hl,BUFA
        call    kanji_rows
        ld      c,0DAh
        ld      a,(k_row)
        ld      d,a
        ld      e,1
        ld      hl,BUFA+800h
        call    kanji_rows
        ld      hl,BUFA+800h
        ld      bc,800h
        call    all_ff
        jr      z,.nx
        ; some machines answer on DAh/DBh with level 1 (a mirror, not level 2)
        ld      hl,BUFA
        ld      de,BUFA+800h
        ld      bc,800h
        call    cmp_mem
        ld      hl,32
        jr      nz,.s
.nx:    ld      a,(k_row)
        add     a,8
        ld      (k_row),a
        cp      64
        jr      c,.n
        ld      hl,16
.s:     ld      (cur_units),hl
        or      a
        ret

; C = first port, D = first row (2 KB each), E = rows, HL = destination
kanji_rows:
        di
.r:     xor     a
        ld      (k_lo),a
.c:     ld      a,(k_lo)
        out     (c),a
        inc     c
        ld      a,d
        out     (c),a
        ld      b,32
        inir
        dec     c
        ld      a,(k_lo)
        inc     a
        ld      (k_lo),a
        cp      64
        jr      nz,.c
        inc     d
        dec     e
        jr      nz,.r
        ei
        ret

rd_kanji:
        ld      a,(ru_u)
        ld      c,0D8h
        cp      16
        jr      c,.j1
        ld      c,0DAh
        sub     16
.j1:    add     a,a
        add     a,a
        ld      d,a
        ld      e,4
        ld      hl,BUFA
        call    kanji_rows
        ld      hl,BUFA
        ret

;------------------------------------------------------------------------------
; 12-dot kanji ROM behind switched I/O F7h (ports 40h-49h). It reads forwards
; only and the first 2 KB cannot be addressed, so they are taken here, before
; anything else moves the pointer.
;------------------------------------------------------------------------------

probe_k12:
        in      a,(40h)
        ld      (k12_prev),a
        ld      a,0F7h
        out     (40h),a
        ld      a,1
        ld      (k12_on),a
        in      a,(40h)
        cp      08h
        jr      z,.found
        call    k12_restore
        ld      hl,m_nok12
        scf
        ret
.found: di
        ld      hl,k12cache
        call    k12_rd2k
        ld      hl,BUFA
        call    k12_rd2k
        xor     a
        ld      b,a
        call    k12_setrc
        ld      hl,BUFA+800h
        call    k12_rd2k
        ei
        ld      hl,BUFA+800h
        ld      de,BUFA
        ld      bc,800h
        call    cmp_mem
        jr      z,.fresh
        call    k12_restore
        ld      hl,m_k12used
        scf
        ret
.fresh: di
        ld      a,37
        ld      b,178
        call    k12_setrc
        ld      hl,BUFA
        call    k12_rd2k
        ei
        ld      hl,BUFA
        ld      bc,800h
        call    all_ff
        ld      hl,32
        jr      nz,.s
        ld      hl,16
.s:     ld      (cur_units),hl
        or      a
        ret

k12_rd2k:
        ld      e,8
k12_rdn:
        ld      c,49h
        ld      b,0
.l:     inir
        dec     e
        jr      nz,.l
        ret

; A = row, B = column. Row is written twice because each write keeps the other
; coordinate from the current pointer, which is meaningless below 800h.
k12_setrc:
        out     (47h),a
        ld      c,a
        ld      a,b
        out     (48h),a
        ld      a,c
        out     (47h),a
        ret

k12_restore:
        ld      a,(k12_on)
        or      a
        ret     z
        xor     a
        ld      (k12_on),a
        ; nothing answered before (FFh): select ID 00h, no device
        ld      a,(k12_prev)
        cpl
        out     (40h),a
        ret

rd_k12: ld      hl,(ru_u)
        ld      a,h
        or      l
        jr      nz,.gen
        ld      hl,k12cache
        ld      de,BUFA
        ld      bc,800h
        ldir
        di
        xor     a
        ld      b,a
        call    k12_setrc
        ld      hl,BUFA+800h
        ld      e,24
        call    k12_rdn
        ei
        ld      hl,BUFA
        ret
.gen:   ; pointer = 800h + 18 * (row * 192 + column) + skip
        ld      a,l
        and     7
        rrca
        rrca
        rrca
        ld      h,a
        ld      a,l
        rrca
        rrca
        rrca
        and     1Fh
        ld      c,a
        ld      l,0
        ld      de,800h
        or      a
        sbc     hl,de
        jr      nc,.nb
        dec     c
.nb:    ld      e,18
        call    div24
        ld      (k12_skip),a
        ld      c,0
        ld      e,192
        call    div24
        ld      b,a
        ld      a,l
        di
        call    k12_setrc
        ld      a,(k12_skip)
        or      a
        jr      z,.ns
        ld      b,a
.sk:    in      a,(49h)
        djnz    .sk
.ns:    ld      hl,BUFA
        ld      e,32
        call    k12_rdn
        ei
        ld      hl,BUFA
        ret

;------------------------------------------------------------------------------
; Panasonic firmware mapper (Panasonic 16/32, WX16). Region 2 (4000h-5FFFh)
; is banked through 6800h and bank bit 8 through 7FF8h. Registers go back
; only if they read back.
;------------------------------------------------------------------------------

probe_pana:
        ld      a,1
        ld      (pana_act),a
        xor     a
        ld      (p_ok),a
        call    sel_p1
        ld      hl,7FF0h
        ld      de,p_raw
        ld      bc,10
        ldir
        ld      a,0FFh
        ld      (7FF9h),a
        ld      hl,7FF0h
        ld      de,p_reg
        ld      bc,9
        ldir
        ; trust the read-back only if a changed register reads back changed
        ld      a,(p_reg+2)
        xor     1
        ld      (6800h),a
        ld      b,a
        ld      a,(7FF2h)
        cp      b
        ld      a,(p_reg+2)
        ld      (6800h),a
        jr      nz,.nrb
        ld      b,a
        ld      a,(7FF2h)
        cp      b
        jr      nz,.nrb
        ld      a,1
        ld      (p_ok),a
.nrb:   call    rst_p1
        ld      a,(p_ok)
        or      a
        jr      nz,.rb
        ; bit 8 of the other regions is then written as 0
        ld      (p_reg+8),a
        inc     a
        ld      (any_noreg),a
.rb:    ld      a,(cur_type)
        ld      b,82h
        cp      27
        jr      z,.s
        ld      b,84h
        cp      68
        jr      z,.s
        ld      b,88h
.s:     ld      a,b
        ld      (p_srame),a
        ld      hl,256
        ld      a,(cur_type)
        cp      142
        jr      z,.u
        ; 4 MB unless bank 100h shows bank 0 again
        ld      hl,100h
        ld      (ru_u),hl
        call    rd_pana
        call    sel_p1
        ld      a,(p_reg+8)
        and     0FBh
        ld      (7FF8h),a
        xor     a
        ld      (6800h),a
        ld      hl,4000h
        ld      de,BUFB
        ld      bc,2000h
        call    cmp_mem
        push    af
        call    rst_p1
        pop     af
        ld      hl,256
        jr      z,.u
        ld      hl,512
.u:     ld      (cur_units),hl
        or      a
        ret

; 7FF9h stays FFh for the whole ROM. At the end region 2,
; bit 8 and 7FF9h are put back only if they read back; otherwise they stay as
; the last bank left them (7FF9h FFh) and the summary asks for a reset.
pana_restore:
        ld      a,(pana_act)
        or      a
        ret     z
pana_fin:
        xor     a
        ld      (pana_act),a
        ld      a,(p_ok)
        or      a
        ret     z
        call    sel_p1
        ld      a,(p_reg+2)
        ld      (6800h),a
        ld      a,(p_reg+8)
        ld      (7FF8h),a
        call    pana_ctl
        ld      (7FF9h),a
        jp      rst_p1

; The control register reads back only if bit 3 was set before; its value is
; then what 7FF9h gave, and must agree with the registers it made readable.
pana_ctl:
        ld      a,(p_raw+9)
        bit     3,a
        jr      z,.zero
        bit     2,a
        jr      z,.b4
        ld      hl,p_raw
        ld      de,p_reg
        ld      b,8
.c:     ld      a,(de)
        cp      (hl)
        jr      nz,.zero
        inc     hl
        inc     de
        djnz    .c
.b4:    ld      a,(p_raw+9)
        bit     4,a
        ret     z
        ld      a,(p_raw+8)
        ld      b,a
        ld      a,(p_reg+8)
        cp      b
        ld      a,(p_raw+9)
        ret     z
.zero:  xor     a
        ret

; SRAM banks (80h up to p_srame) and RAM banks (180h up) read as FFh.
rd_pana:
        ld      hl,(ru_u)
        ld      a,h
        or      a
        ld      a,l
        jr      nz,.hi
        cp      80h
        jr      c,.rom
        ld      b,a
        ld      a,(p_srame)
        dec     a
        cp      b
        jr      nc,.ff
        jr      .rom
.hi:    cp      80h
        jr      nc,.ff
.rom:   call    sel_p1
        ld      hl,(ru_u)
        ld      a,(p_reg+8)
        and     0FBh
        bit     0,h
        jr      z,.h0
        or      04h
.h0:    ld      (7FF8h),a
        ld      a,l
        ld      (6800h),a
        ld      hl,4000h
        ld      de,BUFB
        ld      bc,2000h
        ldir
        call    rst_p1
        ld      hl,BUFB
        ret
.ff:    ld      hl,BUFB
        ld      bc,2000h
        call    fill_ff
        ld      hl,BUFB
        ret

;------------------------------------------------------------------------------
; FS-A1FM firmware mapper: region 2 is banked through 6000h
;------------------------------------------------------------------------------

probe_a1fm:
        xor     a
        ld      (a1_ok),a
        call    sel_p1
        ld      a,04h
        ld      (7FF9h),a
        ld      a,(7FF2h)
        ld      (a1_r2),a
        xor     1
        ld      (6000h),a
        ld      b,a
        ld      a,(7FF2h)
        cp      b
        ld      a,(a1_r2)
        ld      (6000h),a
        jr      nz,.n
        ld      b,a
        ld      a,(7FF2h)
        cp      b
        jr      nz,.n
        ld      a,1
        ld      (a1_ok),a
.n:     xor     a
        ld      (7FF9h),a
        call    rst_p1
        ld      a,(a1_ok)
        or      a
        jr      nz,.u
        inc     a
        ld      (any_noreg),a
.u:     ld      hl,128
        ld      (cur_units),hl
        or      a
        ret

rd_a1fm:
        call    sel_p1
        ld      a,(ru_u)
        ld      (6000h),a
        ld      hl,4000h
        ld      de,BUFB
        ld      bc,2000h
        ldir
        ld      a,(a1_ok)
        or      a
        jr      z,.nr
        ld      a,(a1_r2)
        ld      (6000h),a
.nr:    call    rst_p1
        ld      hl,BUFB
        ret

;------------------------------------------------------------------------------
; FS-A1FM modem ROM: 4000h-5FFFh banked through 7FC4h
;------------------------------------------------------------------------------

probe_modem:
        xor     a
        ld      (md_ok),a
        call    sel_p1
        ld      a,(7FC4h)
        ld      (md_orig),a
        ; trusted only if a changed value reads back changed
        xor     1
        ld      (7FC4h),a
        ld      b,a
        ld      a,(7FC4h)
        cp      b
        ld      a,(md_orig)
        ld      (7FC4h),a
        jr      nz,.n
        ld      b,a
        ld      a,(7FC4h)
        cp      b
        jr      nz,.n
        ld      a,1
        ld      (md_ok),a
.n:     call    rst_p1
        ld      a,(md_ok)
        or      a
        jr      nz,.u
        inc     a
        ld      (any_noreg),a
.u:     ld      hl,16
        ld      (cur_units),hl
        or      a
        ret
rd_modem:
        call    sel_p1
        ld      a,(ru_u)
        ld      (7FC4h),a
        ld      hl,4000h
        ld      de,BUFB
        ld      bc,2000h
        ldir
        ld      a,(md_ok)
        or      a
        jr      z,.nr
        ld      a,(md_orig)
        ld      (7FC4h),a
.nr:    call    rst_p1
        ld      hl,BUFB
        ret

;------------------------------------------------------------------------------
; HAL-Note mapper (Sony firmware): window 4000h-5FFFh banked through 4FFFh.
; The register cannot be read back, so 0 is written at the end.
;------------------------------------------------------------------------------

probe_hal:
        ld      hl,128
        ld      (cur_units),hl
        or      a
        ret

rd_hal: call    sel_p1
        ld      a,(ru_u)
        ld      (4FFFh),a
        ld      hl,4000h
        ld      de,BUFB
        ld      bc,2000h
        ldir
        xor     a
        ld      (4FFFh),a
        call    rst_p1
        ld      hl,BUFB
        ret

;------------------------------------------------------------------------------
; TC8566AF disk ROM. 7FF0h-7FFFh hold the FDC registers, so they are never read
; and are written as FFh. Type 143 banks 16 KB through 7FF0h and is put back to
; bank 0; type 57 is never switched.
;------------------------------------------------------------------------------

probe_tc:
        xor     a
        ld      (tc_orig),a
        ld      hl,2
        ld      a,(cur_type)
        cp      143
        jr      nz,.u
        ld      a,1
        call    tc_mirror
        ld      hl,2
        jr      z,.u
        ld      a,2
        call    tc_mirror
        ld      hl,4
        jr      z,.u
        ld      hl,8
.u:     ld      (cur_units),hl
        or      a
        ret
; A = bank. Z when it shows the same data as bank 0.
tc_mirror:
        ld      (tc_bank),a
        call    sel_p1
        ld      a,(tc_bank)
        ld      (7FF0h),a
        ld      hl,4000h
        ld      de,BUFB
        ld      bc,3FF0h
        ldir
        xor     a
        ld      (7FF0h),a
        ld      hl,4000h
        ld      de,BUFB
        ld      bc,3FF0h
        call    cmp_mem
        push    af
        ld      a,(tc_orig)
        ld      (7FF0h),a
        call    rst_p1
        pop     af
        ret

rd_tc:  ld      a,(ru_u)
        ld      b,a
        and     1
        ld      (tc_half),a
        srl     b
        ld      a,b
        ld      (tc_bank),a
        call    sel_p1
        ld      a,(cur_type)
        cp      143
        jr      nz,.nb
        ld      a,(tc_bank)
        ld      (7FF0h),a
.nb:    ld      a,(tc_half)
        or      a
        ld      hl,4000h
        ld      bc,2000h
        jr      z,.h0
        ld      hl,6000h
        ld      bc,1FF0h
.h0:    ld      de,BUFB
        ldir
        ld      a,(tc_half)
        or      a
        jr      z,.nf
        ld      a,0FFh
        ld      b,16
.f:     ld      (de),a
        inc     de
        djnz    .f
.nf:    ld      a,(cur_type)
        cp      143
        jr      nz,.nr
        ld      a,(tc_orig)
        ld      (7FF0h),a
.nr:    call    rst_p1
        ld      hl,BUFB
        ret

;------------------------------------------------------------------------------
; Small helpers
;------------------------------------------------------------------------------

; C:HL / E -> C:HL quotient, A remainder
div24:  xor     a
        ld      b,24
.l:     add     hl,hl
        rl      c
        rla
        jr      c,.s
        cp      e
        jr      c,.n
.s:     sub     e
        inc     l
.n:     djnz    .l
        ret

; Z when HL = DE, CY when HL < DE
cmp_hl_de:
        ld      a,h
        cp      d
        ret     nz
        ld      a,l
        cp      e
        ret

cmp_hl_bc:
        ld      a,h
        cp      b
        ret     nz
        ld      a,l
        cp      c
        ret

; HL at digits -> DE value, HL past them
read_num:
        ld      de,0
.l:     ld      a,(hl)
        call    isdigit
        ret     nc
        push    hl
        ld      h,d
        ld      l,e
        add     hl,hl
        add     hl,hl
        add     hl,de
        add     hl,hl
        sub     '0'
        ld      e,a
        ld      d,0
        add     hl,de
        ex      de,hl
        pop     hl
        inc     hl
        jr      .l

; CY when A is a digit
isdigit:
        cp      '0'
        ccf
        ret     nc
        cp      '9'+1
        ret

; Z at the end of a line
is_eol: or      a
        ret     z
        cp      0Dh
        ret     z
        cp      0Ah
        ret     z
        cp      1Ah
        ret

; Upper case, and "_" for what an FCB name cannot hold (marks the entry renamed).
fcbchar:
        call    upcase
        push    hl
        push    bc
        ld      hl,s_badfcb
        ld      b,a
.l:     ld      a,(hl)
        or      a
        jr      z,.ok
        inc     hl
        cp      b
        jr      nz,.l
        ld      b,'_'
        ld      (ix+E_TRUNC),1
.ok:    ld      a,b
        pop     bc
        pop     hl
        cp      ' '
        ret     nc
        ld      (ix+E_TRUNC),1
        ld      a,'_'
        ret

upcase: cp      'a'
        ret     c
        cp      'z'+1
        ret     nc
        sub     20h
        ret

putc:   push    af
        push    bc
        push    de
        push    hl
        push    ix
        push    iy
        ld      c,2
        ld      a,(is_dos2)
        or      a
        jr      nz,.c
        ; DOS 1: direct output, so that BDOS itself never acts on CTRL+C
        ld      c,6
.c:
        call    BDOS
        pop     iy
        pop     ix
        pop     hl
        pop     de
        pop     bc
        pop     af
        ret

puts:   ld      a,(hl)
        or      a
        ret     z
        ld      e,a
        call    putc
        inc     hl
        jr      puts

crlf:   ld      e,13
        call    putc
        ld      e,10
        jp      putc

print_dec:
        xor     a
print_dec_w:
        ld      (pd_w),a
        ld      de,decbuf+5
        xor     a
        ld      (de),a
.l:     push    de
        ld      c,0
        ld      e,10
        call    div24
        pop     de
        dec     de
        add     a,'0'
        ld      (de),a
        ld      a,h
        or      l
        jr      nz,.l
        ld      hl,decbuf+5
        or      a
        sbc     hl,de
        ld      a,(pd_w)
        sub     l
        jr      c,.np
        jr      z,.np
.p:     push    af
        push    de
        ld      e,' '
        call    putc
        pop     de
        pop     af
        dec     a
        jr      nz,.p
.np:    ex      de,hl
        jp      puts

getkey: push    bc
        push    de
        push    hl
        ld      c,8
        ld      a,(is_dos2)
        or      a
        jr      nz,.c
        dec     c
.c:
        call    BDOS
        pop     hl
        pop     de
        pop     bc
        jp      upcase

readline:
        ld      de,lbuf
        ld      c,0Ah
        call    BDOS
        jp      crlf

summary:
        call    crlf
        ld      a,(n_done)
        ld      l,a
        ld      h,0
        call    print_dec
        ld      hl,m_sum1
        call    puts
        ld      a,(n_skip)
        ld      l,a
        ld      h,0
        call    print_dec
        ld      hl,m_sum2
        call    puts
        ld      a,(any_noreg)
        or      a
        ld      hl,m_noreg
        call    nz,puts
        ld      a,(any_parts)
        or      a
        ret     z
        ld      hl,m_join
        jp      puts

;------------------------------------------------------------------------------
; Tables
;------------------------------------------------------------------------------

; type, method, units (nominal 8 KB units; for linear types the size cap)
typetab:
        DB      24,M_KANJI
        DW      32
        DB      30,M_K12
        DW      32
        DB      27,M_PANA
        DW      512
        DB      68,M_PANA
        DW      512
        DB      142,M_PANA
        DW      256
        DB      139,M_A1FM
        DW      128
        DB      140,M_MODEM
        DW      16
        DB      40,M_HAL
        DW      128
        DB      57,M_TC
        DW      2
        DB      143,M_TC
        DW      8
        DB      64,M_LIN
        DW      2
        DB      65,M_LIN
        DW      2
        DB      67,M_LIN
        DW      2
        DB      29,M_UNSUP
        DW      0
        DB      31,M_UNSUP
        DW      0
        DB      79,M_UNSUP
        DW      0
        DB      82,M_UNSUP
        DW      0
        DB      96,M_UNSUP
        DW      0
        DB      107,M_UNSUP
        DW      0
        DB      50,M_UNSUP
        DW      0
        DB      16,M_UNSUP
        DW      0
        DB      0FFh,0
        DW      0

; type, offset from the start of the ROM, length: registers that are not read
holetab:
        DB      64
        DW      3F80h
        DB      40h
        DB      65
        DW      3FF8h
        DB      8
        DB      67
        DW      3F80h
        DB      40h
        DB      105
        DW      3FF0h
        DB      8
        DB      106
        DW      3FF0h
        DB      8
        DB      28
        DW      7FFFh
        DB      1
        DB      0FFh
        DW      0
        DB      0

probetab:
        DW      probe_lin,probe_kanji,probe_k12,probe_pana
        DW      probe_a1fm,probe_modem,probe_hal,probe_tc
readtab:
        DW      rd_lin,rd_kanji,rd_k12,rd_pana
        DW      rd_a1fm,rd_modem,rd_hal,rd_tc
fintab:
        DW      nothing,nothing,k12_restore,pana_restore
        DW      nothing,nothing,nothing,nothing
methnames:
        DW      n_lin,n_kanji,n_k12,n_pana,n_a1fm,n_modem,n_hal,n_tc

n_lin:          DB      "linear",0
n_kanji:        DB      "Kanji ROM, I/O",0
n_k12:          DB      "12-dot Kanji ROM, I/O",0
n_pana:         DB      "Panasonic mapper",0
n_a1fm:         DB      "FS-A1FM mapper",0
n_modem:        DB      "FS-A1FM modem mapper",0
n_hal:          DB      "HAL-Note mapper",0
n_tc:           DB      "TC8566AF banks",0

s_slots:        DB      "[SLOTS]",0
s_badfcb:       DB      " +,.:;<=>?[]|*",22h,0

m_title:        DB      "blueMSX+ BIOS dumper",13,10,0
m_usage:        DB      "Usage: BIOSDUMP [config file] [d:] [/Y] [/S]",13,10
                DB      "  config file  default CONFIG.INI",13,10
                DB      "  d:  drive to write the ROM files to",13,10
                DB      "  /Y  start without asking",13,10
                DB      "  /S  always write 128 KB parts",13,10,0
m_tpa:          DB      "Not enough memory: needs a 64 KB MSX.",13,10,0
m_askcfg:       DB      "Config file (ENTER = CONFIG.INI): ",0
m_askdrv:       DB      "Drive to save to (ENTER = current): ",0
m_nocfg:        DB      "Cannot open ",0
m_baddrv:       DB      "The drive to save to is not ready.",13,10,0
m_noent:        DB      "No ROM files in the [Slots] section.",13,10,0
m_plan:         DB      13,10,"ROM files in the config:",13,10,0
m_slot:         DB      "  slot ",0
m_type:         DB      "  type ",0
m_kb:           DB      " KB",0
m_r_nomap:      DB      "skip: not in memory",0
m_r_unsup:      DB      "skip: not supported, use another tool",0
m_r_dup:        DB      "skip: same file as another entry",0
m_proceed:      DB      13,10,"Write them to drive "
m_pdrv:         DB      "A:? (Y/N) ",0
m_cpusw:        DB      "R800 DRAM mode: reading in R800 ROM mode.",13,10,0
m_cpufail:      DB      "Could not leave R800 DRAM mode: page 0 and",13,10
                DB      "4000h-7FFFh of the system ROMs come from RAM.",13,10,0
m_trunc:        DB      "  Name shortened; rename it on the PC to ",0
m_skipped:      DB      "  Skipped: ",0
m_size:         DB      "  Size: ",0
m_writing:      DB      "  Writing ",0
m_wfail:        DB      "  Write failed (disk full?); file deleted.",13,10,0
m_parts1:       DB      "  Writing ",0
m_parts2:       DB      " parts of 128 KB, ",0
m_parts3:       DB      " KB in all.",13,10,0
m_pfail:        DB      "  Disk full; this part was deleted.",13,10,0
m_swap1:        DB      "  Not enough room. Put a disk with free",13,10
                DB      "  space in drive "
m_sdrv:         DB      "A: and press a key (ESC skips this ROM)",13,10,0
m_aborted:      DB      "  Stopped: this ROM is INCOMPLETE. ",0
m_aborted2:     DB      " part(s) were written;",13,10
                DB      "  do not join them.",13,10,0
m_break:        DB      13,10,"Stopped by CTRL+C.",13,10,0
m_brkf6:        DB      13,10
m_brkf:         DB      "The unfinished file was deleted.",13,10,0
m_abinc:        DB      13,10,"The ROM being written in parts is INCOMPLETE:",13,10,"do not join its parts.",13,10,0
m_needy:        DB      "Output is redirected, so nothing can be asked: add /Y.",13,10,0
m_noswap:       DB      "  Not enough room, and with the output redirected no other",13,10
                DB      "  disk can be asked for.",13,10,0
m_deleted:      DB      "  Deleted a part left by an earlier run: ",0
m_nodel:        DB      "  Could not delete the old part ",0
m_keep0:        DB      "  Kept (looks like a part from another run): ",0
m_kmore1:       DB      "  ... and ",0
m_kmore2:       DB      " more such files kept.",13,10,0
m_keep:         DB      "  Kept (an earlier part, of this or another run): ",0
m_over:         DB      "Warning: more than 32 ROM files or a name longer",13,10
                DB      "than 26 characters; see the list.",13,10,0
m_cut:          DB      "  (cut at 26 characters)",0
m_renamed:      DB      "  Same 8.3 name as another file; written as ",0
m_noreg:        DB      "  The mapper registers could not be read back;",13,10
                DB      "  reset the MSX before using its firmware.",13,10,0
m_nokanji:      DB      "no Kanji ROM answers on I/O D8h/D9h",0
m_nok12:        DB      "no 12-dot Kanji ROM on switched I/O F7h",0
m_nomus:        DB      "no ROM at 4000h, or disabled",0
m_k12used:      DB      "12-dot ROM was read since power-on; reset and run again",0
m_sum1:         DB      " ROM file(s) written, ",0
m_sum2:         DB      " skipped.",13,10,0
m_join:         DB      "Join the parts on the PC with join.bat or join.ps1.",13,10,0

;------------------------------------------------------------------------------
; Variables
;------------------------------------------------------------------------------

is_dos2:        DB      0
cur_drv:        DB      0
w_fresh:        DB      0
w_open:         DB      0
ab_in:          DB      0
cp_kept:        DB      0
pana_act:       DB      0
brk_file:       DB      0
sb_t:           DW      0
w_inparts:      DB      0
cp_mode:        DB      0
n_over:         DB      0
cp_n:           DB      0
cp_p:           DW      0
uq_d:           DB      0
uq_pos:         DW      0
p_ok:           DB      0
a1_ok:          DB      0
md_ok:          DB      0
any_noreg:      DB      0
fcb_srch:       DS      37
quiet:          DB      0
msxver:         DB      0
f_yes:          DB      0
f_split:        DB      0
cfg_given:      DB      0
drv_given:      DB      0
t_drive:        DB      0
drvletter:      DB      'A'
cpu_orig:       DB      0
cpu_sw:         DB      0
n_ent:          DB      0
n_done:         DB      0
n_skip:         DB      0
any_parts:      DB      0
insec:          DB      0
cur_meth:       DB      0
cur_slot:       DB      0
cur_page:       DB      0
cur_type:       DB      0
cur_units:      DW      0
cur_name:       DS      11
ru_u:           DW      0
w_u0:           DW      0
w_u1:           DW      0
w_u:            DW      0
w_n:            DW      0
w_part:         DW      0
fr_clu:         DW      0
fr_cls:         DW      0
cr_src:         DW      0
cr_dst:         DW      0
cr_len:         DW      0
lin_src:        DW      0
lin_dst:        DW      0
lin_pre:        DW      0
hole_abs:       DW      0
hole_len:       DB      0
k_lo:           DB      0
k_row:          DB      0
p3_a8:          DB      0
p3_sub:         DB      0
k12_prev:       DB      0FFh
k12_on:         DB      0
k12_skip:       DB      0
p_raw:          DS      10
p_reg:          DS      9
p_srame:        DB      0
a1_r2:          DB      0
md_orig:        DB      0
tc_orig:        DB      0
tc_bank:        DB      0
tc_half:        DB      0
pd_w:           DB      0
cfg_size:       DW      0
p_start:        DW      0
p_end:          DW      0
p_dot:          DW      0
b_start:        DW      0
nums:           DS      10
decbuf:         DS      6
cfgname:        DB      "CONFIG.INI",0
                DS      21
tokbuf:         DS      32
lbuf:           DB      30,0
                DS      42
fcb_cfg:        DS      37
fcb_out:        DS      37

code_end:
enttbl          EQU     code_end
k12cache        EQU     enttbl+ENTSZ*MAXENT
mem_end         EQU     k12cache+800h

        ASSERT  mem_end <= BUFA
