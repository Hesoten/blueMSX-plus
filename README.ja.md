# blueMSX+

blueMSX+ は MSX エミュレータ [blueMSX](https://msxblue.com/bluemsx/jindex.htm) の非公式フォークです。  
Windows 11 向けに UI や音声周りを中心にモダン化しています。


## v3.1.1 リリースの変更点

- 同期モードが「**Windowsに同期**」(v3.1.0 以降の既定) のときに、ウィンドウを最小化すると異常終了する不具合を修正
- デバッガのステップバックが徐々に遅くなる不具合を修正


## v3.1.0 リリースの変更点

- 同梱する C-BIOS に FDD と turbo R への対応を追加 ([v0.29+](https://github.com/Hesoten/cbios-nextor_FDD-and-turboR))
- ショートカットや MSX のキーボード・コントローラボタンに、最大 3 つまでのキーやボタンを割り当てられるようにしました
- カセットテープ機能の対応強化 — cas / wav 形式での読み書きと、tsx 形式の読み込みに対応
- MSX マウスの速度の改善と調整機能の追加
- MoonSound (OPL4) の音質を向上
- Direct3D 12 のスケーリングフィルタを追加
- Flash-ROM SCC (Developer Edition) 対応の追加
- コマンドラインオプションの拡充
- Program Files 以下にインストールした場合などに、意図せずマシン構成フォルダが `<ドキュメントフォルダ>` 以下になってしまう不具合を修正
- デバッガの UI および各種不具合の修正
- ROM 自動判別の改善と、zip 版データベースが読み込めていなかった不具合の修正・更新
- VDP の不具合の修正 (Blink 関連など)
- 言語によっては UI に文字が収まらない箇所があったのを改善
- その他、多数の細かな修正・改善

全ての変更履歴は [`blueMSX/changes.txt`](blueMSX/changes.txt) を参照してください。


## オリジナル blueMSX に対する主な改善点

- **Windows 11 ネイティブ対応**
  - 64 bit アプリケーション化、UI ダークモード対応などモダンな Windows 機能に対応
- **高解像度ディスプレイ対応**
  - 8倍までのウィンドウ拡大や DPI スケーリングに対応
- **Direct3D 12 対応**
- **WASAPI 対応**
  - 低レイテンシなオーディオ出力が可能な WASAPI に対応 (共有モードのみ)
- **MSX-MUSIC / MSX-AUDIO マルチバックエンド**
  - [Nuked-OPLL](https://github.com/nukeykt/Nuked-OPLL)、[emu2413](https://github.com/digital-sound-antiques/emu2413)、[emu8950](https://github.com/digital-sound-antiques/emu8950) などの高音質 FM 音源エミュレータを追加搭載
  - リアルタイム切替(聴き比べ)可能
- **HDR によるスキャンライン描画時の自然な明るさ補正**
- **TMS9918A (MSX1 用 VDP)の発色再現**
  - TMS9918A オリジナルの発色を再現 ([uniskie 氏のパッチ](https://uniskie.hatenablog.com/entry/ar1884677)をマージ)
- **録画機能モダン化**
  - MP4 (H.264 または HEVC) でのライブ録画やリプレイ動画の書き出しに対応
  - リプレイ動画書き出し時のプレビュー表示
- **XInput コントローラー + hot-plug 対応**
- **MegaFlashROM SCC+ SD カートリッジ対応**
  - SD カードを含めた MegaFlashROM SCC+ SD の多様な機能をエミュレート  
    (カートリッジ搭載 PSG ポートの MSX 内蔵 PSG ポートへの上書きは未サポート)
- その他のバグ修正・改善


## 動作環境

- Windows 11 : 64bit 版 blueMSX+ の主要な機能が動くことを確認しています
- Windows 10 (64bit) : おそらく動作しますが、未確認です
- Windows 10 (32bit) : 32bit 版 blueMSX+ が動作する可能性がありますが、未確認です
- Direct3D 12 対応 GPU と HDR 対応ディスプレイを推奨


## 注意事項/免責事項

- MSX は MSX ライセンシングコーポレーションの登録商標です。

- blueMSX+ は正常動作を保証しない、**無保証**のソフトウェアです。本ソフトウェアの使用によって生じたいかなる損害(データ消失・ハードウェア損傷・経済的損失等を含み、これらに限られない)について、**blueMSX+ およびオリジナル版 blueMSX の開発者・コントリビューターは一切の責任を負いません。**

- blueMSX+ は blueMSX の**非公式フォーク**です。**本ソフトウェアに関する問い合わせをオリジナル版 blueMSX の開発チームや、MSX 関連各社・各団体に行わないでください**。

- **OLED (有機ELディスプレイ) での HDR モードの使用について**  
  HDR によるスキャンライン描画時の明るさ補正機能は、スキャンラインで暗くなった画面を補うために、通常ピクセルより高い輝度を局所的に使います。**過度に高い輝度設定での同一内容の表示や連続使用は、OLED ディスプレイの焼き付きを進行させる可能性が考えられます**。ディスプレイ側の保護機能と併用し、使わないときには実行を終了することを推奨します。


## ライセンス

- GPLv2 ライセンスのソースコードを含むため、blueMSX+ 全体としては GPLv2 でライセンスします。誰でも自由に複製、改変、配布する事が出来ますが、改変した実行ファイルを配布する場合は、改変後のソースコードを公開するなど GPL に準拠した扱いが必要です。
- GPLv2 の全文は https://www.gnu.org/licenses/old-licenses/gpl-2.0.html を参照してください。
- C-BIOS 利用マシン構成に同梱のファイルは、それぞれ以下のライセンスに従います。
  - C-BIOS: 各 C-BIOS マシン構成フォルダの `cbios.txt`
  - Nextor: C-BIOS FDD 版マシン構成フォルダの `LICENSE-Nextor.md`
  - TC8566AF FDC ドライバ: C-BIOS FDD 版マシン構成フォルダの `LICENSE-TC8566AF.txt`
- 「漢字ROM image file for msx emulaters」のライセンスは `Machines/Shared Roms/LICENSE-KANJI.txt` を参照してください。


## インストール方法

1. [Releases](https://github.com/Hesoten/blueMSX-plus/releases) からリリースアーカイブをダウンロードする
   - 通常は 64bit 版のアーカイブ (`x64`) を、32bit 版が必要な場合は `Win32` のアーカイブをダウンロードしてください
2. 好きな場所に展開する
3. (オプション) お持ちの MSX 実機に対応した BIOS ファイルを、Machines フォルダの該当機種のフォルダに配置する (config.ini 内に記述された BIOS ファイル名と一致するよう配置)
4. 展開したフォルダ内の `blueMSX+.exe` を起動する


### 既存の blueMSX / blueMSX+ 環境を流用する場合

引き継ぎたい設定ファイルがある既存の blueMSX / blueMSX+ フォルダをお持ちの場合は、リリースアーカイブの中身を **フォルダ構成を保ったまま** そのフォルダに上書きコピーし、`blueMSX+.exe` を起動してください。

※ `blueMSX+.exe` を起動すると、既存の blueMSX 用の設定ファイル (`*.ini`) は blueMSX+ 用にアップデートされます。この結果、同フォルダ内のオリジナル `blueMSX.exe` は正常に起動・動作しなくなる可能性があります。


## おすすめ設定

blueMSX+ の起動後、メニューの `オプション` から以下の設定を行うと、より高品位な描画や音声を楽しめます。

### Direct3D 12 レンダラ

`オプション` → `ビデオ` の `ドライバ` で **Direct3D 12** を選択してください。  
HDR 出力・高品位スキャンラインと明るさ補正・モニタエミュレーション・ライブ録画・リプレイの動画書き出し等が利用可能になります。

### WASAPI (低レイテンシ音声)

`オプション` → `サウンド` の `ドライバ` で **WASAPI** を選択してください。  
`サウンドバッファ` をお使いの PC 環境に合わせて短く設定することで、より低遅延の音声が楽しめます。

※ 実際に使用されるバッファサイズはお使いの PC のサウンドハードウェアの性能によって決まります。(`実バッファ: NN ms` と表示されます)。これより低い値を設定しても、内部では実バッファサイズに切り上げられます。

### MSX-MUSIC / MSX-AUDIO バックエンド (お好みで)

`オプション` → `サウンド` の `MSX-MUSIC バックエンド` / `MSX-AUDIO バックエンド` から、使用する FM 音源エミュレータを選択できます。

- **Nuked-OPLL**: Nuke.YKT さんによる高精度な YM2413 実装
- **emu2413**: Mitsutaka Okazaki さんによる高品質な YM2413 実装
- **emu8950**: Mitsutaka Okazaki さんによる高品質な Y8950 実装
- **openMSX**: openMSX の MSX-AUDIO 実装を取り込んだもの
- **original blueMSX**: オリジナル blueMSX の旧来実装

音を出せる(聞ける)バックエンドは MSX-MUSIC、MSX-AUDIO それぞれ一つずつです。  
複数のバックエンドを有効化した場合は、**有効化された全てのバックエンドで同時にエミュレーションを行います**。**その分 CPU 負荷がかかりますが、** ホットキーや設定ダイアログ上でリアルタイムに出音バックエンドを切り換えての聴き比べが可能になります。お好みのものを決めたら、それを既定に設定(その他のバックエンドを無効化)してください。


## MegaFlashROM SCC+ SD の使い方

1. MegaFlashROM SCC+ SD の openMSX 用ファイルを [MSX Cartridge Shop](https://www.msxcartridgeshop.com) からダウンロード
   - Flash → MegaFlashROM SCC+ SD → openMSX ROM (`mfrsd.zip`) をダウンロード
2. `mfrsd.zip` を展開して、中の `mfrsd.rom` を blueMSX+ の `Machines/Shared Roms/` フォルダに置く
3. blueMSX+ を起動し、メニュー `ROMスロット1 (または 2)` → `特殊カートリッジ` → `Mega Flash ROM SCC+ SD` を選択
4. メニュー `ファイル` → `ハードディスク / SDカード` から空イメージ作成または既存イメージファイルを指定して SD カードを挿入


## ビルド方法

- **Visual Studio 2022** または **Visual Studio 2026** で対応するソリューションファイル(`Make/msvc2022/blueMSX.sln` または `Make/msvc2026/blueMSX.sln`)を開いてビルドしてください。


## 今後追加したい機能 (候補)

- デバッグ関連機能の拡充 (VRAM・スプライト viewer、トレーサーなど)
- ディレクトリマウント機能の改良 (書き出し・大容量ディスクサポート)
- ネットワーク対応の改善
- データレコーダ UI の追加
- 巻き戻し再生 UI (OSD?) の追加
- VDP 描画タイミングの精度向上
- エミュレーション速度の改善
- vgm 録音


## 謝辞

素晴らしい MSX エミュレータを開発された、Daniel Vik さんらオリジナル blueMSX の開発チームおよびコントリビューターの皆様に深く感謝いたします。

blueMSX+ の機能拡張やデバッグにあたっては、openMSX を参考にさせて頂きました。  
継続的に素晴らしい MSX エミュレータの開発を続けられている開発メンバーおよびコントリビューターの皆様にも、敬意と感謝を表します。

Nuked-OPLL は Nuke.YKT さんの著作物です。  
emu2413 および emu8950 は Mitsutaka Okazaki さんの著作物です。  
TMS9918A パッチは uniskie さんの著作物です。  
これらの素晴らしい機能を blueMSX+ に取り込ませていただきました。心より御礼申し上げます。

blueMSX+ には、オープンソースの MSX BIOS 実装である C-BIOS をベースに turbo R やディスク起動への対応を加えたマシン構成を同梱しています。  
これにより、MSX 実機の BIOS ROM を用意しなくても多くのソフトウェアを実行できます。  
(注：MSX-BASIC を利用するものは実行できません)  

C-BIOS の詳細については、Machines フォルダの C-BIOS 利用マシン構成に同梱の `cbios.txt` および
<https://cbios.sourceforge.net/> を参照してください。  
BouKiCHi、Reikan、Maarten ter Huurne、Albert Beevendorp、Patrick van Arkel、Manuel Bilderbeek、Joost Yervante Damad、Jussi Pitkänen、Eric Boon の各氏をはじめとする C-BIOS プロジェクトの皆様に、このような素晴らしい互換 BIOS を開発頂き、また自由に再配布可能な形で公開して頂いていることへの感謝を申し上げます。

C-BIOS FDD 版のマシン構成では、ディスクカーネルとして **Nextor** を使用しています。  
Nextor を開発され、継続的に改良を続けられ、またこのようなプロジェクトが成立する形でソースコードを公開して頂いている Nestor Soriano Vilchez (Konamiman) 氏に、心より御礼申し上げます。

C-BIOS の JP 版および turbo R のマシン構成には、A to C さん制作の「漢字ROM image file for msx emulaters」を使用しています。
パブリックドメインの jiskan16 フォントに MSX 固有グリフを手描きで加えた、自由に再配布可能な漢字フォント ROM を制作された A to C 氏に御礼申し上げます。

MSX Resource Center や GitHub などでフィードバックや要望をくださった皆様にも御礼申し上げます。

blueMSX+ の改良コードは Claude Code で開発を行っています。  
やりたい機能追加や不具合修正を次々と実現していく能力に、驚きと畏怖の念を覚えます。
