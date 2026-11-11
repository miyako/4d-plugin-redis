# 4d-plugin-redis

クラシック4D向けの、ホストにインストールするネイティブRedisクライアントです。
単一のスタンドアロンRedisに接続する型付きコマンドを提供します。PHPやRedis CLIの
起動、ORDAの自動統合、4D内部キャッシュの置き換えは行いません。

[English](README.md) · [依存関係の選定と出所](docs/dependencies.md)

**プレビューリリース [0.1.2](https://github.com/miyako/4d-plugin-redis/releases/tag/v0.1.2)
を公開しました。** macOS universal（arm64/x86_64、デプロイメントターゲット11.0）と
Windows x64のバイナリーを同梱します。macOS配布物はDeveloper ID署名・公証済みで、
Windows実行ファイルは未署名です。対象と実証済みの
環境の違いは[検証](#検証)を参照してください。サポートの下限は、実行環境での確認を
基準に保守的に **4D 21.0** としています。古い4DではSDKをロードできない、という
意味ではありません。`HELLO 3` と `SET KEEPTTL` のプロトコル機能上の下限は
**Redis 6.0** ですが、サーバーの実証済み環境は **Redis 7.2.9のみ** で、
Valkeyは未検証です。Redis ClusterとSentinelには対応しません。

**同期・協調モード専用です。** 協調ワーカーから呼んでも、ネイティブのネットワーク待機が
UIを停止させる可能性があります。

## インストール

[0.1.2のZIP](https://github.com/miyako/4d-plugin-redis/releases/download/v0.1.2/redis-0.1.2.zip)
（macOS／Windows共通）、またはmacOS用の
[DMG](https://github.com/miyako/4d-plugin-redis/releases/download/v0.1.2/redis-0.1.2.dmg)
をダウンロードします。[SHA256SUMS](https://github.com/miyako/4d-plugin-redis/releases/download/v0.1.2/SHA256SUMS)
と出所・公証の記録もリリースページから取得できます。

ZIPを展開するか、macOSでDMGをマウントし、`redis.bundle` ディレクトリー全体を
ホストの `Plugins` にコピーして4Dを再起動します。このディレクトリーには
`Contents/MacOS/redis`、`Contents/Windows64/redis.4DX` と必要なリソースを同梱します。
実行ファイルだけをコピーしないでください。
クライアント／サーバー構成では、コマンドを実行する各ホストにインストールします。
接続ハンドルはデータベースやリモートクライアントではなく、そのランタイムに属します。
再配布時は、同梱のライセンスと帰属表示のファイルを保持してください。

[ソースからビルド](#ビルドとテスト)する場合は、生成された `redis.bundle`（macOS）、
または `Contents/Windows64/redis.4DX` を含む `redis` フォルダー全体（Windows）を
コピーします。ローカルビルドやCIのテスト用アドホック署名を、
正式な署名・公証済み配布物とみなさないでください。

## 使用例

```4d
var $r; $cache; $value; $closed : Object
var $connection : Integer

$cache:=REDIS Open(New object("host"; "127.0.0.1"; "port"; 6379; \
    "connectTimeout"; 1000; "commandTimeout"; 500))
If ($cache.ok)
    $connection:=$cache.connection
    // NXと有効期限を、1回のアトミックなSETに指定
    $r:=REDIS Set text($connection; "example:greeting"; "Hello"; \
        New object("condition"; "NX"; "ttlMilliseconds"; 60000))
    If ($r.ok)
        $value:=REDIS Get text($connection; "example:greeting")
        If ($value.ok)
            If ($value.found)
                // 空のテキストとキャッシュミスは区別されます。
            End if
        End if
    End if
    $closed:=REDIS Close($connection)
End if
```

他のプロパティを解釈する前に必ず `ok` を確認してください。書き込み失敗をミスと
扱わず、各呼び出しのエラーを処理します。キーにアプリケーション固有の名前空間を
設け、Redis ACLは最小権限で設定してください。

## コマンド

すべてのコマンドは **Object** を返します。グローバルな最終エラーはありません。
成功時は `{ok:true}` と次のフィールドを返します。接続はポインターではなく、
不透明な正の32ビットInteger/Longint IDです。

| コマンド | connection以降の引数 | 成功時の追加フィールド |
|---|---|---|
| `REDIS Open` | `options : Object` のみ（省略可） | `connection : Integer` |
| `REDIS Close` | なし | なし |
| `REDIS Connection info` | なし | `host`, `port`, `database`, `tls`, `connected`, `connectTimeout`, `commandTimeout` |
| `REDIS Get text` | `key : Text` | `found : Boolean`。存在する場合のみ `value : Text` |
| `REDIS Set text` | `key : Text; value : Text; options : Object`（省略可） | `applied : Boolean` |
| `REDIS Get blob` | `key : Text; value : Blob`（**書き込み可能な出力**） | `found : Boolean`。データはBLOB引数に格納 |
| `REDIS Set blob` | `key : Text; value : Blob; options : Object`（省略可） | `applied : Boolean` |
| `REDIS Delete` | Textの `keys : Collection` | `value : Text`、10進数の件数（DEL） |
| `REDIS Exists` | Textの `keys : Collection` | `value : Text`、10進数の件数（EXISTS） |
| `REDIS Increment` | `key : Text; delta : Text` | `value : Text`、正確な符号付きint64（INCRBY） |
| `REDIS Expire` | `key : Text; milliseconds : Text` | `applied : Boolean`（PEXPIRE） |
| `REDIS TTL` | `key : Text` | `value : Text`、ミリ秒。`"-1"` は期限なし、`"-2"` は存在しない（PTTL） |
| `REDIS Persist` | `key : Text` | `applied : Boolean`（PERSIST） |
| `REDIS Ping` | なし | `value : Text`、`"PONG"` |

Open以外の最初の引数は常に `connection : Integer` です。Delete/Existsは
1〜10000個のTextキーを受け取ります。重複キーの件数はRedis自身の仕様に従います。
空のキーと値も使用できます。
特にGet blobの書き込み可能な出力など、必須引数はすべて渡してください。
4Dはプラグインの省略引数に型の既定値を補い、コンパイルエラーではなく警告にする
場合があります。省略を意図しているのは、上記で明示した末尾のoptions Objectだけです。

Connection infoはローカル情報だけを返します。PING、再接続はせず、ユーザー名、
パスワード、TLSパス、クライアント名も返しません。`connected` はローカルで接続を
無効化していないことを示し、現在のサーバーの応答性を保証しません。無効化後、
明示的にCloseする前のハンドルは `connected:false` を返します。

### TextとBLOB

Textは正規化せずUTF-8に変換します。サロゲートペアと埋め込みNULも保持します。
不正なUTF-16入力は拒否します。Get textは不正なUTF-8を置換・切り捨てず、
`invalid_utf8` を返します。任意のバイト列にはGet blobを使用してください。
4Dの通常のText比較は照合規則に従うため、厳密なテストには文字コードを比較します。

```4d
var $bytes : Blob
var $r : Object
$r:=REDIS Get blob($connection; "image:42"; $bytes)
If ($r.ok)
    If ($r.found)
        // NULを含む元のバイト列が$bytesに格納されます。
    End if
End if
```

マニフェストでは実際の **`&O` BLOB引数** として宣言しています。Pointerや
オブジェクトプロパティではありません。Get blobが正常にミスした場合は出力を空にし、
`found:false` を返します。存在する長さ0の値は空のBLOBと `found:true` を返します。
失敗時は呼び出し元のBLOBを変更しません。Set blobも型付きBLOBを直接受け取り、
base64には変換しません。

### アトミックなSETオプション

| プロパティ | 型と意味 |
|---|---|
| `ttlMilliseconds` | 正の整数Number（最大2147483647）、または符号付きint64最大値までの正規10進Text。PXを指定 |
| `condition` | 正確に `"NX"`（未存在時のみ）または `"XX"`（存在時のみ） |
| `keepTTL` | Boolean。trueでKEEPTTLを指定 |

未知のプロパティ、型違い、正でないTTL、`ttlMilliseconds` と `keepTTL:true` の
併用は送信前に拒否します。条件が成立しない場合は `ok:true, applied:false` です。
TTL/KEEPTTLなしのSETはRedisの仕様通り既存の期限を削除します。絶対時刻の
オーバーフローを起こすほど大きなTTLはRedis側でも拒否されます。

Incrementは1回のINCRBYで、GET+SETではありません。SETの期限指定も
SET+EXPIREではありません。deltaは必須で、単純な加算には `"1"` を渡します。
カウンター、delta、Expireの値は正規10進Text（`"0"`、`"-1"` など）です。
先頭のゼロ、プラス符号、空白、指数、小数は認めません。Expireは正の値のみで、
即時削除にはDeleteを使用します。int64の範囲は
`"-9223372036854775808"` 〜 `"9223372036854775807"` です。
入力範囲外は `integer_overflow`、Redisの加算オーバーフローはサーバーエラーです。
黙って丸めるReal/NumberのカウンターAPIはありません。

## Open設定

| プロパティ | 既定値／制約 |
|---|---|
| `host` | `"127.0.0.1"`。空でないDNS名またはIPリテラル。NUL不可 |
| `port` | `6379`。整数Number、1〜65535 |
| `database` | `0`。Longint範囲の非負整数Number |
| `connectTimeout` | `1000`。整数Number、1〜60000ミリ秒 |
| `commandTimeout` | `1000`。整数Number、1〜60000ミリ秒 |
| `username`, `password` | Text。パスワードのみならACLユーザーdefault。ユーザー名にはパスワード必須 |
| `clientName` | 省略可能なText。Redisの名前の制約に従う |
| `tls` | `false`。Boolean |
| `caFile` | 任意のPEM信頼ファイル。ネイティブOSパス。システムルートの代わりに使用 |
| `certFile`, `keyFile` | 任意のPEMクライアント証明書／秘密鍵。ペア指定、TLS必須 |

未知のプロパティ、null、型違いはエラーです。省略した設定には既定値を使用します。
`tls:false` でのTLSファイルや、証明書／鍵の片方だけの指定は拒否します。
認証情報をホスト名やキーに含めないでください。URIは解釈しません。
DNS名はASCII／punycodeで指定します。プラグインによるIDNA変換は行いません。

Openは接続、**RESP3** の明示的なネゴシエーション（`HELLO 3`、必要ならAUTH）、
データベース選択、任意のクライアント名設定、PING確認後にハンドルを返します。
RESP3はライブラリー選定に合わせ、初期案のRESP2から意図的に変更しました。
プッシュは非対応で、受信時には接続を破棄します。バックグラウンドPING、再接続、
書き込みの再試行・再実行は行いません。

TLSは1.2以上で、証明書チェーンとDNS名／IP SANの両方を検証します。DNS名では
SNIを送信し、IPリテラルではIPとして検証しSNIを送りません。caFileがない場合は
macOS Security／Windows ROOTストアのルート証明書をOpenSSLに取り込みます。
検証はOpenSSLによるもので、OS固有のポリシー・失効確認と同じではありません。
プライベートCAにはcaFileを指定します。クライアント秘密鍵は対話的なパスフレーズ
なしで読み込める必要があり、入力要求はしません。検証無効化オプションはありません。

## 所有権・タイムアウト・エラー

ハンドルはOpenした **4Dプロセス／ワーカー** 内で保持します。他プロセスからの
操作・Closeは拒否します。IDはランタイム中に再利用しません。正のLongint範囲を
使い切った場合はランタイムを再起動します。クライアント／サーバー間、再起動、
再ロードをまたいで転送できません。共有プールはありません。

呼び出しは同期的で、全コマンドは **`threadSafe:false`** です。プリエンプティブ
対応は保証しません。所有権管理には専用の協調ワーカーを使用し、期限を短くし、
UIプロセスでの呼び出しを避けてください。ただし協調ワーカーでもネイティブの
ブロッキングはUI応答に影響し得ます。実行器はプラグイン呼び出し中にのみ動作し、
ライブラリーのスレッドから4D SDKを呼ぶことはありません。

connectTimeoutはDNS、TCP、TLS、Redis設定、初期PINGのネットワーク処理全体を、
commandTimeoutは送信・受信全体を制限します。段階ごとの独立した加算ではありません。
ハードリアルタイム保証ではなく、ローカル証明書／ルート読み込みやOSの名前解決の
後処理に追加時間がかかる場合があります。要求引数の総バイト数と応答読み込みバッファーは
64 MiBに制限します。CloseはQUITを送信せず、Redisの応答を待ちません。

必ず明示的にCloseしてください。アンロード時はレジストリーを解放します。
SDKのkCreateProcess/kCloseProcessでも所有者単位で解放しますが、すべての終了方法を
同一と仮定せず、実行環境での確認範囲は検証ドキュメントを参照してください。

失敗時の形式:

```json
{"ok":false,"code":"command_timeout","message":"...","outcome":"unknown","connectionUsable":false}
```

| outcome | 意味 |
|---|---|
| `not_submitted` | 指定したキャッシュ操作は未送信。引数／所有者／ハンドル不正、Open失敗、無効化済み接続など。Open内部のセッション設定は実行済みの可能性あり |
| `server_error` | Redisのエラー応答。ミスではなく、ストリームは再使用可能 |
| `received` | 応答は消費済みだが、ローカル変換・型検証が失敗 |
| `unknown` | 送信／実行済みの可能性があるが、有効な応答がない |

**タイムアウトや応答消失は、書き込みが実行されなかった証拠ではありません。**
Incrementなどを無条件に再試行しないでください。タイムアウト、切断、不正／未対応の
応答、プッシュで通信状態を破棄します。新しい接続を明示的にOpenします。自動再接続・
再実行はありません。connectionUsableはローカル状態であり、引数エラー時には指定した
ハンドルの有効性を検証しません。Open失敗では使用可能な接続を返しません。
ネイティブ内部例外の場合は現在の所有者の接続をすべて破棄します。

主なコード: invalid_arguments、integer_overflow、invalid_handle、wrong_owner、
tls_configuration、open_failed、open_timeout、redis_error、command_timeout、
connection_failure、connection_closed、unsupported_push、unexpected_reply、
invalid_utf8、sdk_error。認証情報や不正な文字列の混入を防ぐため、サーバーの
診断文字列はそのまま返したりログに出したりしません。
メモリー不足でエラーObject自体も作成できない場合は、標準エラーに固定メッセージを
出力してNullを返し、C++例外をSDK境界の外へ伝播させません。

## 対象範囲

Memcached、Cluster/Sentinel探索、リダイレクト追従、Pub/Sub、MONITOR、トランザクション、
購読、汎用executor、カスタム／モジュールコマンド、pipeline APIはありません。
パイプラインとトランザクションは別の概念です。固定APIからOpen後に
AUTH/SELECT/HELLO/QUITや応答モード変更を送信することはできません。

## ビルドとテスト

CMake 3.28以上、C++20コンパイラー、Python 3.11以上、Perl、Gitが必要です。
macOSはXcodeコマンドラインツール、Windowsはvswhereを含むVisual Studio x64
C++ツールが必要です。TLSテストにはPATH上のopensslコマンドも必要です。
リンクするOpenSSL自体は固定ソースからビルドし、HomebrewやPATHのライブラリーは
使用しません。固定入力・ライセンスは依存関係ドキュメントを参照してください。

```sh
git clone --recurse-submodules https://github.com/miyako/4d-plugin-redis.git
cd 4d-plugin-redis
cmake -S redis -B redis/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build redis/build --config Release --parallel
ctest --test-dir redis/build -C Release --output-on-failure
```

Windowsでは `cmake -S redis -B redis/build -A x64` の後に同じbuild/ctestコマンドを
実行します。OpenSSLは明示的にvcvars64を呼び出し、アセンブラーを不要にし、
Debug側も含めて静的 `/MT` CRTを使用します。

ネイティブテストはint64、Unicode、SET引数、移植可能なソケット／TLSフィクスチャを
検証しますが、実Redisの代替ではありません。実Redisとマニフェスト／SDK経由のテスト:

```sh
python3 scripts/test_redis.py --server /path/to/redis-server \
  --native redis/build/redis_native_tests \
  --tool4d /path/to/tool4d --password disposable-test-only
```

このスクリプトは一時Redisを自身で起動・終了し、データベース1と名前空間付きキーを
使用します。tool4dの終了状態と正確なPASS行の両方を確認します。本番認証情報は
渡さないでください。`--tls` を追加すると一時的なlocalhost証明書を生成し、
TLS対応サーバーでも確認できます。オフラインのtool4dテスト:

```sh
/path/to/tool4d --dataless --startup-method=test_all \
  --project="$PWD/redis/redis-test/Project/redis.4DProject"
```

ライブ4DテストはREDIS_TEST_PORTが必要です。REDIS_TEST_HOST、REDIS_TEST_PASSWORD、
REDIS_TEST_TLS_CAも指定可能です。ポート指定なしのPASSはオフラインの受け入れ確認のみで、
実Redisの動作確認を意味しません。

### 検証

実施済みの検証と未検証のプラットフォーム／配布条件は
[docs/validation.md](docs/validation.md)に記録します。リリースCIではmacOS／Windowsの
ネイティブテストとオフラインtool4d確認、およびmacOSの署名済みパッケージの
スモークテストが成功しています。Windowsの実際の4D Text／BLOB APIとワーカー終了処理を
実Redisに接続して確認する検証は未実施です。4D 21.0は実行確認による
保守的な下限です。SDK経由で呼ぶ最も新しい言語コマンドは18 R3導入のOB Keys
（1719）ですが、それだけで以前の全バージョンをサポートするとは判断しません。

## CI/CDと配布

[4d-plugin-tutorial](https://github.com/miyako/4d-plugin-tutorial/blob/main/docs/plugin-pipeline.md)
の正規パイプラインから **Bump version**、**Build and Test**、
**Build and Release** の3ワークフローを生成しています。VERSIONが唯一の版数源で、
バージョン更新だけでは公開しません。テストには署名シークレットを渡さず、
両対象OSでCTest、パッケージ契約テスト、オフラインtool4dを実行します。
実Redis確認は上記の明示的な別コマンドで、CI成功から暗黙に推定しません。

`python3 scripts/check_pipeline.py` で生成ファイルの所有権を確認します。
再生成には記録したtutorial版のジェネレーターと `.github/plugin-pipeline.json` を
使用し、管理対象ワークフローを手編集しません。Boost/OpenSSLは静的リンクで、
動的依存はOSライブラリーのみです。統合パッケージは両アーキテクチャ、システムのみの
依存、改変されていない通知、出所、同じ実行の成果物を検証します。

リリースはmainから手動で行い、release-signing環境に
APPLE_DEVELOPER_ID_CERTIFICATE、APPLE_DEVELOPER_ID_CERTIFICATE_PASSWORD、
KEYCHAIN_PASSWORD、NOTARYTOOL_APPLE_ID、NOTARYTOOL_TEAM_ID、NOTARYTOOL_PASSWORD
が必要です。正式な名前は生成されたワークフローを参照してください。設定はユーザーが
行い、エージェントは認証情報を読み取り・アップロードしません。リリース処理は
macOS配布物のDeveloper ID署名・公証、DMGへのチケットのステープルと評価、
アーカイブ検証、署名済みプラグインのスモークテストを公開前に実行します。
検証済みのドラフトリリースを作成し、メンテナーが公開します。
Windows実行ファイルは未署名のままです。macOSバンドルの署名は、
WindowsのAuthenticode署名ではありません。

## ライセンスと帰属

MITです。LICENSEには4D SDKのMIT通知も含めます。スキャフォールドと生成パイプラインは
miyako/4d-plugin-tutorial由来です。Boost.Redis/BoostはBSL-1.0、
OpenSSLはApache-2.0です。THIRD-PARTY-NOTICESの通知は正規パッケージ処理で同梱します。
Redisサーバー自体は同梱しません。
