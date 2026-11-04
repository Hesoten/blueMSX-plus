# geo3d fill spans as HMMV (V9968_Cartridge v0.92) on blueMSX+

Local change on top of `experimental/v9968-geo3d-v2.0` (`da1c7bd`).

HRA's V9968_Cartridge v0.92 (2026-11-01, commit `3d55af4`) speeds up
SCREEN 8 polygon drawing: the geo3d engine still emits a horizontal LINE
for an untextured filled-face span, but it also raises a tag
(`cmd_fill_span`), and the V9968, seeing the tag at command start, runs
the span as a one-row HMMV instead. The HMMV path writes the colour
directly, where LINE reads every destination pixel back first. This
change gives the emulator the same behaviour, in the same places the
FPGA has it:

| File | Change |
|---|---|
| `Geo3DCore.h`, `Geo3DCore.cpp` | `Command` gains a `fillSpan` tag, set only in `renderFaces` for untextured spans. The emitted bytes stay a LINE, bit for bit. Savestate layout 5 stores the tag; older layouts load with it clear. |
| `Geo3D.h`, `Geo3D.cpp` | `geo3dPeekFillSpan`, and `geo3dConvertFillSpan`: the rewrite itself, shared so a host test can drive it without the emulator. |
| `VDP.h`, `VDP.c` | `vdpIsV9968Screen8()`: the mode test. `screenMode == 8` here is Graphic7 with YJK off; the YJK screens are reported as 10/12, which excludes them just as the FPGA's YJK flag does. |
| `Geo3DDevice.c` | At issue time, a tagged command is converted before its registers are written. Device savestate version 5 (matching core layout 5). |

The conversion fires only when every condition the FPGA checks holds:
tagged span, LINE with LOP = IMP, ARG = 0 (rightward, downward, X-major,
no destination-memory bit), NY = 0, DX inside the 256-dot screen, and
SCREEN 8 at issue time. Then NX grows by 1 (LINE includes its endpoint,
HMMV counts pixels), NY becomes 1, and the command byte becomes HMMV.

Notes and differences from the FPGA:

- The command still lands as 11 register writes, so the device timing
  plan (`plan()` gives an 11-byte command 14 ec to land) is untouched.
  What changes is the consumer side: `V9938.c` already carries separate
  LINE and HMMV costs, fitted to the RTL for the V9968 high-speed model,
  so a converted span is charged the HMMV cost and a RUN ends sooner,
  as on a v0.92 cartridge.
- The FPGA keeps the post-command register values at their LINE values
  and falls back to LINE if the screen mode changes mid-span. Neither
  is observable here: the command registers are write-only from the
  CPU, geo3d rewrites all of them for the next span, and the command
  engine in this emulator keeps the mode a command started with.
- CPU-issued LINE commands are never converted: the tag exists only on
  geo3d commands, and only the device consults it.

Checks done so far (Linux, g++):

- `geo3d_hmmv_test/test_geo3d_hmmv.cpp`, built against `Geo3D.cpp` and
  `Geo3DCore.cpp` alone, passes: a flat quad gives 31 tagged spans with
  the expected LINE bytes; every span converts in SCREEN 8 and draws
  the same pixel set as its LINE; a wireframe edge whose bytes have the
  same shape is not tagged and not converted; textured (LRMM) commands
  are not tagged; LOP != IMP, DX >= 256, NY != 0, ARG != 0 are all
  refused; a savestate taken mid-RUN keeps the tags of the commands
  still to be issued.

  ```sh
  g++ -std=c++20 -O1 -I blueMSX/Src/VideoChips -I blueMSX/Src/Common \
      geo3d_hmmv_test/test_geo3d_hmmv.cpp \
      blueMSX/Src/VideoChips/Geo3D.cpp blueMSX/Src/VideoChips/Geo3DCore.cpp \
      -o test_geo3d_hmmv && ./test_geo3d_hmmv
  ```

- `Geo3DDevice.c` and `VDP.c` pass a C syntax check with the project's
  include paths, with no warnings beyond the ones the unmodified files
  already produce.

Still open: the full Windows build (Visual Studio 2022,
`blueMSX/Make/msvc2022/blueMSX.sln`, Release|x64, as in GEO3D_V2.md)
and on-screen checks against the demos, which is what this copy is for.

---

## 日本語版 / Japanese version

# blueMSX+ での geo3d ベタ塗りスパンの HMMV 化（V9968_Cartridge v0.92）

`experimental/v9968-geo3d-v2.0`（`da1c7bd`）の上に載せた変更です。

HRA 氏の V9968_Cartridge v0.92（2026-11-01、コミット `3d55af4`）は SCREEN 8 の
ポリゴン描画を高速化しました。geo3d エンジンはテクスチャなし面のスパンに
対して従来どおり水平 LINE を出しますが、同時にタグ（`cmd_fill_span`）も
立てます。V9968 はコマンド開始時にタグを見ると、そのスパンを1行 HMMV として
実行します。HMMV は色を直接書き込むため、LINE のように描画先の画素を
1つずつ読み戻す段階がありません。本変更は、FPGA が変換を行うのと
同じ場所で、エミュレータにも同じ振る舞いを持たせるものです。

| ファイル | 変更内容 |
|---|---|
| `Geo3DCore.h`、`Geo3DCore.cpp` | `Command` に `fillSpan` タグを追加し、`renderFaces` のテクスチャなしスパンでのみ立てる。出力されるバイト列は LINE のまま1ビットも変えない。セーブステートのレイアウト5でタグを保存し、旧レイアウトはタグをクリアして読み込む。 |
| `Geo3D.h`、`Geo3D.cpp` | `geo3dPeekFillSpan` と、変換本体 `geo3dConvertFillSpan`。ホスト側の単体テストがエミュレータなしで駆動できるよう、共有関数として切り出した。 |
| `VDP.h`、`VDP.c` | 画面モード判定 `vdpIsV9968Screen8()`。ここでの `screenMode == 8` は「Graphic7 かつ YJK オフ」を意味する。YJK の画面は 10/12 として報告されるため、FPGA の YJK フラグと同じく対象から外れる。 |
| `Geo3DDevice.c` | 発行時に、タグ付きコマンドをレジスタへ書く前に変換する。デバイスのセーブステート版数を5に（コアのレイアウト5と対）。 |

変換が発動するのは、FPGA が確認する条件が全て揃ったときだけです：
タグ付きスパンであること、LINE かつ論理演算（LOP）＝ IMP であること、
ARG ＝ 0（右向き・下向き・X 主軸・宛先メモリビットなし）、NY ＝ 0、
DX が 256 ドットの画面内にあること、そして発行時点で SCREEN 8 であること。
変換では NX が1増え（LINE は終点を含み、HMMV は画素数で数えるため）、
NY が1になり、コマンドバイトが HMMV になります。

FPGA との差異と注意点：

- コマンドは従来どおり11回のレジスタ書き込みで着地するため、デバイス側の
  発行計画（`plan()` が11バイトのコマンドに14 ec を割り当てる）は不変です。
  変わるのは消費側で、`V9938.c` は LINE と HMMV のコストを別々に持ち、
  V9968 高速モデルについて RTL（レジスタ転送レベル記述）に合わせて
  調整済みです。変換されたスパンは HMMV のコストで課金され、
  v0.92 の実カートリッジと同様に RUN の終了が早まります。
- FPGA はコマンド終了後のレジスタ値を LINE の値のまま保持し、
  スパンの途中で画面モードが変わると LINE へフォールバックします。
  どちらもエミュレータでは観測できません。コマンドレジスタは CPU からは
  書き込み専用で、geo3d は次のスパンのために全レジスタを書き直しますし、
  このエミュレータのコマンドエンジンはコマンドが開始した時点の
  画面モードを保持するためです。
- CPU が自分で発行した LINE コマンドは決して変換されません。
  タグは geo3d のコマンドにだけ存在し、それを参照するのはデバイスだけです。

これまでに済んでいる確認（Linux、g++）：

- `geo3d_hmmv_test/test_geo3d_hmmv.cpp` を `Geo3D.cpp` と `Geo3DCore.cpp`
  だけでビルドした単体テストが合格しています。フラットな四角形で
  31本のタグ付きスパンが期待どおりの LINE バイト列で得られること、
  全スパンが SCREEN 8 で変換され、元の LINE と同じ画素集合を描くこと、
  バイト列の形が同じワイヤーフレームの辺はタグが付かず変換されないこと、
  テクスチャ面（LRMM）のコマンドにタグが付かないこと、
  LOP ≠ IMP・DX ≥ 256・NY ≠ 0・ARG ≠ 0 が全て拒否されること、
  RUN の途中で取ったセーブステートでも未発行コマンドのタグが保持されること。
- `Geo3DDevice.c` と `VDP.c` は、プロジェクトのインクルードパスでの
  C 構文チェックを通過し、無変更のファイルが元々出す警告以外の
  新規警告はありません。

未了の項目：Windows でのフルビルド（Visual Studio 2022、
`blueMSX/Make/msvc2022/blueMSX.sln`、Release|x64。GEO3D_V2.md と同じ手順）と、
デモを使った画面上での確認です。このコピーはそのためのものです。
