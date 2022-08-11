# Vowel2D
 
 ![スクリーンショット](media/screenshot.png)

## これ何ですか

2つのバンドパスフィルタを声のフォルマント共鳴器に見立てて母音の再現を試みるツールです。オーディオプラグインまたはスタンドアロンの形式で動作します。  
JUCE frameworkを利用しています。

## ビルドの道具立て

* C++ビルドツール: Visual Studio 2017～2022、Xcodeなど、目的のターゲットプラットフォームに合わせたもの。
* JUCE framework: [ダウンロードページ](https://juce.com/get-juce/download)または[githubリポジトリ](https://github.com/juce-framework/JUCE)からダウンロードできます。
* Projucer: JUCE frameworkを使ったC++プロジェクトを生成するツールです。[Projucerページ](https://juce.com/discover/projucer)から実行可能なバイナリをダウンロードできますが、JUCE frameworkに同梱のソースを自分でビルドしても良いです。
* ASIO SDK (optional): Windows版のスタンドアロン形式でASIOを有効化できます。[デベロッパページ](https://www.steinberg.net/developers/)からダウンロードできます。

## ビルドの手順

1. Projucerで.jucerファイルを開く。
2. Modulesセクションにおいて、ビルド環境に合わせてmoduleパスを修正する。
3. Exporterセクションにおいて、目的のターゲットプラットフォームを追加する。
4. File|Save ProjectメニューでC++プロジェクトを書き出す。
5. Buildsフォルダ下に書き出されたC++プロジェクトをビルドする。

以下の環境でコードを書き、動作を確認しました。
* Windows 11 Pro
* Visual Studio 2022
* JUCE framework 7.0.1
* プラグインホスト: JUCE frameworkに同梱のAudioPluginHostアプリケーション
* VST3、スタンドアロン形式のビルド

MacやLinuxでもビルドできると思います。(要確認)

## 使い方

プラグインやプラグインホストの基本的な知識については、ここでは省きます。  

* 入力信号にフォルマントフィルタを掛けます。外部入力のほか、内蔵のパルス音源をMIDIで演奏することが出来ます。
* 上段の母音ペインをクリックまたはドラッグすることでバンドパスフィルタの中心周波数を移動します。横と横がそれぞれ第一・第二フォルマントです。
* 中段の周波数特性ペインで個々のバンドパスフィルタの中心周波数とゲインを操作します。出力のスペクトルもここに表示されます。
* 下段のツマミで個々のパラメータをチューニングします。数値入力も出来ます。出力ゲインもここで設定します。

## 作者

[yu2924](https://twitter.com/yu2924)

## ライセンス

MIT License
