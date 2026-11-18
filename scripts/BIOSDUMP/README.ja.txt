====================================================
 blueMSX+ BIOS SLOT DUMPER  (BIOSDUMP.COM)
====================================================

このプログラムは実機の MSX 上で動作し、blueMSX+ の Machines/<機種>/config.ini を
読み込んで、各 [Slots] エントリの BIOS ROM を吸い出します。

実機での動作確認は FS-A1GT (MSX-DOS 2) でのみ行っています。


著作権と法的注意
----------------

BIOS ROM は各メーカーの著作物です。
ご自身が所有する実機から吸い出し、個人利用の範囲でのみ使用してください。
吸い出した ROM ファイルの配布・共有・アップロードは行わないでください。


ファイル
--------

- BIOSDUMP.COM  MSX-DOS で実行するプログラム
- BIOSDUMP.ASM  BIOSDUMP.COM のソースコード (Z80 アセンブリ、sjasmplus 用)
- build.ps1     BIOSDUMP.ASM から BIOSDUMP.COM を生成する PowerShell
                スクリプト (PC で実行。sjasmplus が必要)
- join.ps1 / join.bat
                分割して書き出された ROM を 1 つに結合する
                PowerShell スクリプト / バッチファイル (PC で実行)


対応環境
--------

- MSX1 / MSX2 / MSX2+ / MSXturboR (Z80、R800)
- MSX-DOS 1、MSX-DOS 2、Nextor
- RAM 64KB
- 書き込み先のドライブ (フロッピーディスク、SD カードなど)


使い方
------

1. MSX で使用するディスク (フロッピーディスク、SD カード、
   WiFi カートリッジなど) のルートに、次のファイルを保存します。

   - MSX-DOS のシステムファイル
       MSX-DOS 2: MSXDOS2.SYS と COMMAND2.COM
       Nextor:    NEXTOR.SYS (または MSXDOS2.SYS) と COMMAND2.COM
       MSX-DOS 1: MSXDOS.SYS と COMMAND.COM
   - BIOSDUMP.COM
   - 対象機種の config.ini (blueMSX+ の Machines\<機種名>\ に格納されています)

2. そのディスクで MSX を起動し、MSX-DOS のプロンプト (A> など) が
   表示されたら、次のように入力して BIOSDUMP を実行します。

     A> BIOSDUMP

3. 最初に「Config file (ENTER = CONFIG.INI):」と表示されます。
   CONFIG.INI を読み込むときは、何も入力せずに ENTER を押します。
   別のファイルを読み込むときは、そのファイル名を入力して ENTER を
   押します。
   次に「Drive to save to (ENTER = current):」と表示されます。
   保存先のドライブ (A-H) を入力して ENTER を押します。カレント
   ドライブに保存するときは、何も入力せずに ENTER を押します。

4. 吸い出す ROM ファイルの一覧が表示されます。
   Y を押すと吸い出しを開始し、N または ESC を押すと中止します。
   各 ROM を順に吸い出し、「  64/256 KB」のように進捗を表示します。

5. 保存先のディスクの空き容量が不足すると、保存できた分を
   128KB ずつの分割ファイルとして書き出し、次のメッセージを
   表示して一時停止します。

     Not enough room. Put a disk with free
     space in drive B: and press a key (ESC skips this ROM)

   空き容量のある別のディスクに入れ替えてキーを押すと、残りを
   書き出します。ESC を押すと、その ROM の吸い出しを中断し、
   次の ROM に進みます。詳しくは「分割ファイルとディスクの
   入れ替え」を参照してください。

6. 全 ROM の吸い出しと、PC 上での分割ファイルの結合が終わったら、
   blueMSX+ の Machines/<機種名>/ に
   config.ini と同じファイル名で配置してください。

7. blueMSX+ のメニュー「オプション」→「エミュレーション」→
   「マシン構成」で、その機種を選びます。実機と同じタイトルロゴや
   起動画面が表示されれば成功です。


コマンドラインでの指定
----------------------

手順 3〜4 で質問される内容は、BIOSDUMP を実行するときに、
コマンド名に続けて指定することもできます。指定した項目は
質問されません。

     BIOSDUMP [config ファイル] [d:] [/Y] [/S]

     d:   保存先のドライブ
     /Y   質問せずに吸い出しを開始する
     /S   空き容量があっても 128KB ずつに分割して書き出す
     /?   使い方を表示する

例えば、次のように指定します。

     BIOSDUMP CONFIG.INI B: /Y

MSX-DOS 2 / Nextor では BIOSDUMP CONFIG.INI B: /Y > LOG.TXT のように
して、画面の表示をファイルに保存できます。この場合は質問を表示
できないため、/Y が必要です。ディスクの入れ替えも要求できないため、
空き容量が不足した ROM は中断し、次の ROM に進みます。


生成されるファイル
------------------

config.ini の [Slots] セクションで、ファイル名が書かれている行ごとに、
その 8.3 形式のファイル名 (例: A1WSBIOS.ROM) で作成します。

[Slots] の各行は、先頭から <スロット> <サブスロット> <開始ページ>
<ページ数> <ROM の種類> "<ファイル名>" です。1 ページは 8KB で、
たとえば 0 0 0 4 はスロット 0-0 の 0000h から 32KB を表します。

- 8 文字 (拡張子は 3 文字) を超える名前は切り詰め、
  ファイル名に使用できない文字 (空白や + など) は _ に置き換えます。
  そのときは、PC 上で付け直す元の名前を表示します。
- 切り詰めた結果、別のファイルと同じ名前になる場合は、後から
  書き出すファイルの名前の最後の文字を 1、2 … に変えて書き出し、「Same 8.3 name as
  another file; written as …」と、付け直す元の名前を表示します。
  例: expertAC88+bios.rom、expertAC88+ext.rom、expertAC88+asm.rom は
  EXPERTAC.ROM、EXPERTA1.ROM、EXPERTA2.ROM になります。
- 同じファイル名の行が複数あるときは、範囲が最も大きい行だけを
  吸い出します。


分割ファイルとディスクの入れ替え
--------------------------------

保存先の空き容量が ROM 全体の大きさに満たないとき (または /S を
指定したとき) は、
128KB ごとに <名前>.000、<名前>.001 … (末尾は 16 進) に分けて
書き出します。最初に「Writing 32 parts of 128 KB, 4096 KB in all.」
のように個数と合計を表示します。空き容量が不足したときのディスクの
入れ替えは、「使い方」の手順 5 のとおりです。

- 中断した ROM は「Stopped: this ROM is INCOMPLETE」と表示します。
  書き終えた分割ファイルは残りますが、結合しないでください。
- 入れ替えたディスクに、前回の実行で書き出した同じ名前の分割
  ファイルが残っている場合は、これから書き出す番号以降のものを
  削除し、その旨を表示します。それより前の番号のものは削除せず、
  「Kept …」と表示します。
  入れ替えでは毎回別のディスクを入れてください。

PC では、分割ファイルのあるフォルダで次のどちらかを実行して結合します。

  .\join.ps1 A1GTFIRM.ROM
  join.bat A1GTFIRM.ROM

結合したファイルの大きさが、BIOSDUMP が表示した合計と同じか
確かめてください。join.ps1 は番号の抜けがあると警告します。


CTRL+C での中断
---------------

CTRL+C は、停止するまで押し続けてください。ディスクへの書き込み中に
短く押しただけでは、検出されないことがあります。

停止すると「Stopped by CTRL+C.」と表示し、書き込み途中のファイルを
削除します。分割ファイルの書き出し途中の場合は「INCOMPLETE」と
表示します。書き終えたファイルや分割ファイルは残ります。

MSX-DOS 1 でディスクエラーの「Abort」を選んだときは、書き込み途中の
ファイルが残ることがあります。MSX をリセットして、そのファイルを
削除してください。


対応している ROM
----------------

- メイン ROM、サブ ROM、漢字ドライバ、MSX-MUSIC、DISK ROM などの
  通常の ROM
- 漢字 ROM (第 1 水準・第 2 水準)
- FS-A1FM / FS-4600 系の 12 ドット漢字 ROM
- Panasonic の内蔵ソフト (FS-A1ST・FS-A1GT・FS-A1WX・FS-A1WSX・
  FS-A1FM) とモデム ROM
- ソニーの内蔵ソフト (HB-F1XDJ・HB-F1XV)
- turboR の DISK ROM

次の ROM には対応していません。「not supported, use another tool」
または「not in memory」と表示して、吸い出しをスキップします。

- 辞書 ROM、FS-4600 の内蔵ソフト、MoonSound の音色 ROM、
  MSX-AUDIO の ROM、FM-PAC の ROM、Sunrise IDE の ROM など

スキップされた ROM は、TINY SLOT CHECKER
(https://tiny-yarou.com/slotchecker.html) など他のツールで
吸い出してください。他のツールで吸い出したファイルは、
config.ini に記載されたファイル名に変更してから、
Machines/<機種名>/ に配置してください。


注意事項
--------

- 吸い出したファイルには、FFh で埋めた部分が含まれることが
  あります。FDC のレジスタなど、読み出すとハードウェアの状態が
  変わる部分は、読み出さずに FFh で埋めます。config.ini の範囲の
  うち ROM が存在しない部分も FFh になります。
- 12 ドット漢字 ROM は、12 ドットの文字を表示した後は、吸い出しに
  失敗することがあります。その場合は MSX をリセットしてから、
  最初に BIOSDUMP を実行してください。
- 最後に「The mapper registers could not be read back; reset the
  MSX before using its firmware.」と表示されたら、内蔵ソフトを
  使う前に MSX をリセットしてください。
- turboR では、吸い出しの間だけ CPU を R800 ROM モードに切り替え、
  終わったら元に戻します。


例: ESERAMair と AI エージェントで吸い出す
-----------------------------------------

WiFi カートリッジの ESERAMair (https://github.com/t-bucchi/ESERAMair)
は、MSX からはドライブとして認識され、PC からは
http://eseram.local/ で内部のファイルを読み書きできます。PC 上の
AI エージェント (Claude Code など) に PC 側の作業を任せると、MSX の
前での操作は、BIOSDUMP の実行とキー入力だけになります。FS-A1GT の
内蔵ソフト (4MB) のように ESERAMair に入りきらない BIOS ファイルも、
空き容量が不足するたびにエージェントが回収して消すことで、最後まで
吸い出しを行えます。

エージェントには次の文を渡してください。< > の 3 か所は、
お使いの環境に合わせて書き換えます。

  <blueMSX+ のフォルダ>  blueMSX+ を展開したフォルダ
                         (例: C:\blueMSX+)
  <機種名>               Machines フォルダ内の機種のフォルダ名
                         (例: MSXturboR - Panasonic FS-A1GT)
  <DOS のフォルダ>       MSX-DOS のシステムファイルを格納したフォルダ

----- ここから -----
MSX 実機の BIOS ROM の吸い出しを手伝ってください。私が MSX を
操作し、あなたは PC 側を受け持ちます。手順は
<blueMSX+ のフォルダ>\Tools\BIOSDUMP\README.ja.txt の
「例: ESERAMair と AI エージェントで吸い出す」に記載されています。
対象機種は <機種名>、MSX-DOS のシステムファイルは <DOS のフォルダ>
に格納されています。
----- ここまで -----

以下はエージェント向けの説明です。

[ESERAMair の操作]
8080 番ポートの HTTP で操作します。機器の版で違うことがあるので、
先に http://eseram.local/app.js を読んで確かめてください。
  GET    :8080/fat/status        空き容量 (free_size) など
  GET    :8080/fat/files         ルートのファイル一覧 (JSON)
  GET    :8080/fat/files/<名前>   ファイルを読む
  POST   :8080/fat/files/<名前>   本文をそのまま書く (8.3 の大文字)
  DELETE :8080/fat/files/<名前>   ファイルを消す
書き込みの例 (curl に -G を付けると本文が URL に入るので付けない):
  curl --data-binary @BIOSDUMP.COM http://eseram.local:8080/fat/files/BIOSDUMP.COM

[手順]
1. 「使い方」の手順 1 のファイルを書き (config.ini は CONFIG.INI
   という名前で)、読み戻して確かめる。
2. ユーザーが MSX を起動して BIOSDUMP を実行する。
3. 空き容量が不足すると、BIOSDUMP は 128KB の分割ファイルを
   書き出した時点で、ディスクの入れ替えを待って一時停止する。
   空き容量が 131072 バイト未満で、ファイル一覧が 20 秒ほど
   変化しなければ、一時停止している。書き終えた
   ファイルを PC にコピーし、2 回読んで同じか確かめてから消し、
   ユーザーにキーを押すよう依頼する。
4. 終わったら残りも回収し、分割ファイルは join.ps1 で結合して、
   大きさが BIOSDUMP の表示した合計と同じか確かめる。openMSX の
   share/machines/<機種>.xml の SHA1 とも照合する (openMSX は複数の
   ROM を 1 ファイルにまとめていることがあり、不一致でも誤りとは
   限らない)。

[守ること]
- 確かめる前のファイル、システムファイル、BIOSDUMP.COM、
  CONFIG.INI は消さない。
- ESERAMair の中身を変えるのは、BIOSDUMP が入れ替えを待っている
  ときか、MSX を使っていないときだけ。それ以外で変えたら、
  MSX のリセットを頼む。
- 吸い出した ROM はどこにもアップロードしない。
