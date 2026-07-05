# blueMSX+

blueMSX+ は MSX エミュレータ [blueMSX](https://msxblue.com/bluemsx/jindex.htm) の非公式フォークです。  
Windows 11 向けに UI や音声周りを中心にモダン化しています。


## 主な改善点

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
- **その他のバグ修正・改善**
  - スプライト描画不具合を修正 (拡大スプライトの位置ずれなど)
  - SCREEN 0~4 における VDP コマンドの挙動を修正
  - R800 モードにおけるブロック I/O 命令のフラグ処理を修正
  - TurboR PCM の周波数や書き込みタイミングを修正
  - ディレクトリ挿入時のロングファイルネームへの対応や 720KB オーバー時の警告を追加  
  - 他、多数


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


## インストール方法

[Releases](https://github.com/Hesoten/blueMSX-plus/releases) からリリースアーカイブをダウンロードしてください。  
このアーカイブには blueMSX+ の実行ファイルとデバッグツールプラグイン、キーボード設定ファイルなどが含まれますが、機種定義ファイルや BIOS、テーマなどのデータは同梱していません。
以下のどちらかの方法でオリジナル blueMSX のファイル群を用意し、そこに blueMSX+ のファイルを上書きしてください。

### 方法 A: 新規にインストールする

1. オリジナル blueMSX を [公式サイト](https://msxblue.com/bluemsx/jdownload.html) から入手して、好きな場所に展開する (2.8.2 簡易バージョン - blueMSXv282.zip を推奨)
2. [Releases](https://github.com/Hesoten/blueMSX-plus/releases) からダウンロードした blueMSX+ のリリースアーカイブを、同じく好きな場所に展開する
3. 2.で展開したアーカイブの中身を、1. で展開したオリジナル blueMSX のフォルダに **フォルダ構成を保ったまま** 上書きコピーする (`blueMSX+.exe`、`Tools/*.dll`、`Keyboard Config/` などを 1. のフォルダに上書き)  
4. 1.のフォルダ内で `blueMSX+.exe` を起動する (※ blueMSX.exe は使いません)

### 方法 B: 既存の blueMSX 環境を流用する

1. 既に使っている blueMSX フォルダ (`bluemsx.exe` がある階層) を丸ごとコピーして blueMSX+ 用のフォルダを作成する
2. [Releases](https://github.com/Hesoten/blueMSX-plus/releases) からダウンロードした blueMSX+ のリリースアーカイブを、同じく好きな場所に展開する
3. 2.で展開したアーカイブの中身を、1. で用意した blueMSX+ 用のフォルダに **フォルダ構成を保ったまま** 上書きコピーする (`blueMSX+.exe`、`Tools/*.dll`、`Keyboard Config/` などを 1. のフォルダに上書き)  
4. コピー先の `blueMSX+.exe` を起動する (※ blueMSX.exe は使いません)

※ blueMSX+.exe を起動すると、既存の blueMSX 用の設定ファイル (*.ini) は blueMSX+ 用にアップデートされます。この結果、オリジナルの blueMSX.exe は正常に起動・動作しなくなる可能性があります。


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


## 謝辞

素晴らしい MSX エミュレータを開発された、Daniel Vik さんらオリジナル blueMSX の開発チームおよびコントリビューターの皆様に深く感謝いたします。

blueMSX+ の機能拡張やデバッグにあたっては、openMSX を参考にさせて頂きました。  
継続的に素晴らしい MSX エミュレータの開発を続けられている開発メンバーおよびコントリビューターの皆様にも、敬意と感謝を表します。

Nuked-OPLL は Nuke.YKT さんの著作物です。  
emu2413 および emu8950 は Mitsutaka Okazaki さんの著作物です。  
TMS9918A パッチは uniskie さんの著作物です。  
これらの素晴らしい機能を blueMSX+ に取り込ませていただきました。心より御礼申し上げます。

blueMSX+ の改良コードは Claude Code で開発を行っています。  
やりたい機能追加や不具合修正を次々と実現していく能力に、驚きと畏怖の念を覚えます。
