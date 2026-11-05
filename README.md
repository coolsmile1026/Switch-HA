# Switch HA

Switch（Horizon OS）上的 Home Assistant 瘦客户端。libnx + SDL2 + libcurl 编写。

**界面不是米家风格的翻版**：本项目的 UI 完全按 Home Assistant 官方 Android
Companion App（2026.11.1）反编译出的 `HATheme` 设计系统与 HA frontend 的
信息架构重建，详见下方「界面设计依据」。

接入方式和手机版 HA App 一样：**只需要填服务器网址**，然后在应用内输入
HA 的用户名密码完成登录（走 HA 官方的 `/auth/login_flow` OAuth 流程），
令牌自动保存到 SD 卡，之后每次启动免登录、自动续期。

## 功能

- **左侧 sidebar 导航**（概览 / 自动化 / 设置）+ 地区列表 + 连接状态
- **概览按地区分组的 Tile 卡片**：开启=品牌蓝，关闭=中性灰，带亮度/开合进度
- **类型筛选条 + 搜索 + 收藏**：全部 / 灯 / 开关 / 传感器 / 摄像头 / 收藏
  六个筛选可与地区叠加；Y 键唤出软键盘按名称或 ID 搜索；
  详情页按 Y 收藏常用实体（存 SD 卡，注销不丢），筛选条里一键筛出
- **浅色 / 深色主题**：设置页切换（Y 键，或 ↑ 移到右上角分段控件后
  ←/→ 选择 / A 切换），跟随 HA App 的深色配色（深色下文字已整体提亮），
  偏好存配置文件
- **重开秒进**：已登录时再次打开应用，直接显示上次的设备列表
  （标题栏标「N 分钟前」），启动完全不联网——要看最新状态按 X，
  或在「更换服务器地址」保存后自动同步
- **「更换服务器地址」页可取消**：误碰进来按 B（或点「取消」）直接返回，
  已保存的连接配置保持不变，输入框恢复成当前配置
- **More-info 弹层**：大开关、亮度/开合滑条、实体属性、区域标签
- 灯 / 开关 / 风扇 / 窗帘 / 空调 / 门锁 / 扫地机 / 插座 开关控制
- 传感器状态直接展示（温度、湿度等，带单位）
- 摄像头实时快照（约 1.4 fps 轮询 `/api/camera_proxy`）
- scene / script 在「自动化」页一键执行，官方 Entities card 风格列表
- 长期访问令牌登录（备用，开了两步验证的账号必须用这个）
- 内部 / 外部双网址自动切换；连接性检查逐项诊断
- **应用图标**：hbmenu 列表显示 Home Assistant 官方图标（`icon.jpg`，
  取自 HA 官方 App 的桌面图标合成）
- **OTA 自更新**：设置 → 检查更新，发现新版先弹确认框（升级 / 取消），
  确认后带实时进度条下载并替换自己，
  重启生效，不用反复拷 SD 卡

## 界面设计依据

来源（用户提供的两个站点 + 官方发布包）：

- <https://github.com/home-assistant/android/releases> → `2026.11.1`
  的 `app-minimal-release.apk`（SHA256
  `28abf486cd489ae796c3b4072b2af42b3efdfd149cf82643e92f5ff7656e0215`）
- <https://contributors.home-assistant.io/companion/android/> → 架构、
  flavors、Compose 设计系统文档

APK 用 jadx 反编译到 `analysis/ha_apk_out/`，重点提取了
`sources/io/homeassistant/companion/android/common/compose/theme/`：

| 官方 token | 值 | Switch 上的用途 |
|---|---|---|
| `HABrandColors.Blue` | `#18BCF2` | 主色：按钮、开启态 Tile、选中 chip |
| `HABrandColors.Background` | `#F2F4F9` | 页面背景 |
| `HAColors.Primary40/30` | `#009AC7` / `#006787` | 强调文字、品牌渐变 |
| `HAColors.Primary90/95` | `#DFF3FC` / `#EFF9FE` | 选中底色、开启态卡片 |
| `HAColors.Neutral05..95` | `#141414` … `#F3F3F3` | 文本层级、边框、关闭态 |
| `Green50 / Orange50 / Red50` | `#00883C` / `#C94E00` / `#DC3146` | 成功 / 警告 / 危险 |
| `HARadius` | 4 / 8 / 12 / **16** / 20 / 24 / Pill | 卡片 16、弹层 24、chip 胶囊 |
| `HADimens.SPACE*` | 4 的倍数，8 / 16 / 24 / 32 | 所有间距 |
| `HAFontSize` | 10 / 12 / 14 / 16 / 20 / 24 / 28 / 32 | 屏幕放大后的字号层级 |

组件形态对齐 `HATopBar` / `HAInputChip` / `HATextField` / `HAFilledButton` /
`HASettingsCard` / `HAModalBottomSheet`；错误反馈用深色 snackbar（官方规范
明确要求不用 toast）。文案对齐官方中文资源：*连接到 Home Assistant*、
*Home Assistant 网址*、*地区*、*场景*、*脚本*、*连接性检查*、
*内部网址 / 外部网址*。

> 官方 App 的中心是 WebView 承载 HA frontend，Switch 没有 WebView，
> 所以这里用原生控件复刻 frontend 的信息架构（sidebar + 地区 + Tile 卡片），
> 而不是把米家 App 的三列大卡片换个颜色。

## 使用

1. `switch-ha.nro` 拷到 SD 卡 `sdmc:/switch/`
2. 打开应用，地址页有**两条网址**（对齐官方的 internal / external URL）：
   - **内部网址**：家里用，如 `http://192.168.1.100:8123`（预填的是示例，改成自己的）
   - **外部网址**：出门用，如 `https://ha.example.com`（可留空）
3. 输入 HA 的用户名密码，登录
4. 完成。凭证存在 `sdmc:/switch/switch-ha/auth.txt`

**内外网自动切换**：两条网址都填上后，每次启动都**先试内网**——
内网地址只给 1 秒建立连接（正常局域网是几毫秒）、3 秒内必须给出响应，
超时就立刻换外网重试，切换结果记到配置里。两边是同一台 HA，令牌通用，
切换不需要重新登录。设置页与 sidebar 底部都会标明「内网 / 外网」。

为了不在外网时每个请求都白等一遍内网超时，程序会记住「这次运行内网不通」，
后续请求直接走外网；重开 App（或设置页点「重同步 / 连接性检查」）会重新探测，
所以回到家后自动恢复内网直连。地址在设置页改动后同样会重新探测。

要清掉登录：设置页 → 注销，或直接删 `auth.txt`。

## 手柄操作

主界面焦点分三段：**设备网格 ↔ 顶栏地区切换器 ↔ 左侧 sidebar**，
方向键在边界处会自动跳到相邻区段，蓝色描边指示当前焦点。

| 按键 | 作用 |
|---|---|
| 方向键 / 左摇杆 | 设备区：移动焦点（长按连续移动）/ 调节滑条；最左列按 ← 进 sidebar，第一行按 ↑ 进筛选条 |
| A | 确认（开关设备 / 打开 More-info / 执行场景 / 选中地区 / 应用筛选） |
| B | 返回 / 关闭弹层；主界面**连按两次**退出应用；地址页取消并放弃未提交的输入 |
| L / R | 概览：循环切类型筛选（全部 / 灯 / 开关…）；自动化 / 设置：切页 |
| ZL / ZR | 概览：切换地区（房间）；自动化 / 设置：切页 |
| X | 重新同步 / 摄像头页重连 / 地址页连接性检查 |
| Y | 概览：搜索实体（留空清除）；详情页：收藏 / 取消收藏；设置页：切换浅色 / 深色 |
| + / - | 退出（任何时候，包括同步 / 下载期间长按，回到主循环后立即生效） |

**刷新策略**：启动、挂机都完全不联网。只在两种时机同步：按 X 手动
同步，以及「更换服务器地址」保存后自动拉一次。拉回来的内容如果和缓存
逐字段一致（实体 / 地区 / 场景的顺序和值都没变），不重建列表也不写
SD 卡，只把副标题的「刚刚更新」往前推；只有真的变了才重排并落盘。
要看最新状态按 X 手动同步；副标题会显示「刚刚更新 / N 秒前 / N 分钟前」，
一眼能看出手上这份数据有多旧。

sidebar 内：↑↓ 在「概览 / 自动化 / 设置」和地区列表之间移动，
A 进入，→ 回设备区。地区列表第 0 行固定是「全部地区」，
超过 8 行自动滚动，右侧有滚动条，标题行显示「3–10 / 12」这样的区间。

设备网格是**滚动式**的：焦点走到屏幕边缘时整屏顺势滚一行（标题右侧
显示当前可见区间，如「19 个实体 · 6-19 ↓」），不会再「跳一整页」。
→ 到最右侧一格就停住，不会顺手切换地区（切地区走 ZL/ZR 或侧栏）。

触屏同样可用（sidebar、地区切换器、Tile、弹层滑条都有热区）。

## 构建

```
docker run --rm --user 501:20 -v "$PWD":/src -w /src <devkita64镜像> make
```

桌面预览（macOS，调 UI 用，不联网）：

```
sh desktop/build.sh
./desktop/preview --screen home --shot /tmp/home.bmp
```

`--screen` 支持 setup / setup-empty / login / login-token / home / dev / cam / diag，
`--tab 1` 自动化页，`--tab 2` 设置页，`--sel N` 选中第 N 张 Tile，
`--zone 1/2 --side 4` 预设焦点位置，`--age N` 模拟 N 秒前同步，`--dark` 深色主题（截图验证用）。

## OTA 更新与分发

设置页 → **检查更新**：拉取版本信息 → 比较版本 → **弹窗确认（升级 / 取消）** →
下载新版 .nro（实时进度条）→ 校验 NRO 魔数 → **fsync 落盘并核对大小** →
原子替换（换不动就留到下次启动再换）→ **「更新完成」弹窗**。
整个过程不需要碰 SD 卡，给别人分发后对方也能自助升级。

更新完成后会弹出居中卡片（对勾 + 版本号）：支持 chainload 的环境
（从 hbmenu 启动即是）按钮为**重启应用**——按 A 登记「下一个加载
目标」并退出（退出前有约 1 秒的「正在重启…」，等 SD 卡写完再走，
避免重启回来还是旧版），hbmenu 会直接把新版拉起来，等于原地重启；
不支持的环境按钮显示为**关闭应用**，退出后回 hbmenu 手动再开一次。
按 B 可以「稍后自己重启」，下次启动时依然运行新版。

下载期间屏幕上有**实时进度条**：已下载 / 总大小、百分比、**实时速度**、
**剩余时间**（如 `6.2 / 13.9 MB   45%   1.82 MB/s   剩余 0:04`），
不再是干等的白屏。另外按键路径上不写 SD 卡日志（日志进内存缓冲、
每秒最多落盘一次），方向键连发不再被 SD 卡写拖慢。
14MB 的文件在 Switch 上走 GitHub 大概几十秒到几分钟，取决于网络。

下载用的是「低速检测」而不是固定总超时：连续 60 秒速度低于 1KB/s 才判定
断流，所以慢但稳定的网络也能把 14MB 的新版完整下下来（兜底上限 15 分钟）。
NRO 的体积大头是中文字体，已用 fontTools 子集化（`tools/subset-font.sh`）：
15.7MB → 4.8MB → **1.5MB**（保留 GB2312 全部 6763 个常用汉字 + ASCII +
常用符号/假名，整个 NRO 从 14.6MB 降到 11.1MB，OTA 下载时间约省 1/4）。
GB2312 之外的生僻字（CJK 扩展区、emoji）不在子集里，实体名里如果出现
会显示为方框，常用汉字不受影响。换字体后重跑一次脚本即可。

### 下载线路（国内网络重点）

release 附件由 `github.com` 302 到 `objects.githubusercontent.com`，
这条链路在国内经常极慢或被重置。下载前会先做**线路竞速**：直链、
API、各镜像同时各探测 64KB（约 1~3 秒），**谁先到用谁下整包**，
不再按固定顺序试错；运行中还有**低速熔断**——开跑 10 秒还不足 30KB
的线路立即中断换下一条，不会在死线路上干等。全部失败才报错
（提示里会写明试了几条线路）。

候选线路：GitHub 直链 → API 直连（绕过 302）→ 镜像前缀 + 原地址
（`gh-proxy.com`、`ghfast.top`、`gh-proxy.net`、`ghfile.geekertao.top`、
`ghproxy.net`，全部实测可用，欢迎在 `update.txt` 里自行增删）。

日志里每条线路都有探测结果和失败原因，写在 `sdmc:/switch/mihome/log.txt`
（`net: race [n] ...`、`upd: dl [n] ... failed after Ns`）。

### 更新源

默认指向 `api.github.com/repos/coolsmile1026/Switch-HA/releases/latest`
（见 `source/update.c` 的 `UPD_SRC_DEFAULT`）。
在 SD 卡放 `sdmc:/switch/switch-ha/update.txt` 可以换源：

```
# 第一行：manifest 地址（# 开头的行和空行忽略）
https://api.github.com/repos/coolsmile1026/Switch-HA/releases/latest
# 第二行（可选）：NRO 自身路径，默认 sdmc:/switch/switch-ha.nro
sdmc:/switch/switch-ha.nro
# 其余行（可选）：追加下载镜像前缀，会自动拼在原 GitHub 地址前面
mirror: https://ghfast.top/
mirror: https://your-own-proxy.example.com/
# 想完全关掉内置镜像（网络能直连 GitHub 时）
# mirror: none
```

### 支持的 manifest 格式

1. **GitHub Releases API**（推荐）：直接用 `releases/latest`，
   程序自动取 `tag_name` 作为版本号，`assets` 里第一个 `.nro` 作为下载地址。
   主用 `browser_download_url`，失败时自动回退到 `api.github.com/.../releases/assets/<id>`
   （带 `Accept: application/octet-stream`），避开 `objects.githubusercontent.com` 的 502。
2. **自托管 JSON**（NAS / 任意静态服务都行）：

   ```json
   {"version": "1.0.1", "url": "https://example.com/switch-ha.nro"}
   ```

3. **纯文本**：第一行版本号，第二行下载地址。

### 发新版

一条命令搞定（编译 + 元数据校验 + 打 tag + 建 Release + 上传附件）：

```sh
./tools/release.sh                 # 只编译，产物在 dist/switch-ha-v<版本>.nro
./tools/release.sh --publish                         # 顺带发布（令牌存钥匙串后无需环境变量）
```

手动流程：

1. 改 `Makefile` 的 `APP_VERSION`（如 `1.0.1`，比较时忽略 `v` 前缀）
2. `make` 编译出 `switch-ha.nro`
3. GitHub 发 release，tag 填 `v1.0.1`，把 nro 作为附件上传，
   文件名**必须以 `.nro` 结尾**（建议 `switch-ha-v1.0.1.nro`）

发版四条铁律：

- **仓库不能是空的**。GitHub 的空仓库连 tag 都建不了，必须先有一个 commit
  （哪怕只是一份 README），否则 Releases 页面会一直建不成功。
- **附件里的 NRO 必须自带同一个版本号**。`APP_VERSION` 改成什么，tag 就发什么；
  版本对不上，客户端会每次都提示有新版、下载完还是旧版。
  用 `tools/release.sh` 会自动校验 NACP 里的版本与 Makefile 一致。
- **不要勾 Pre-release**。`/releases/latest` 只返回正式版，勾了预发布就永远查不到。
- **Release 必须点 Publish**。停在 Draft 的 release 对外不可见，API 也查不到。


## 目录

```
source/
  main.c   入口：SDL 初始化、手柄/触控分发、动作队列、OTA 待更新应用
  ui.c     全部界面绘制与交互
  ha.c     HA 协议：登录流、令牌、实体同步、控制
  net.c    libcurl 封装
  json.c   极简 JSON 解析器
  cam.c    摄像头取图线程
  update.c OTA 自更新（manifest 解析、下载、原子替换）
  gfx.c    渲染（字体、圆角、图标、文本缓存）
icon.jpg   hbmenu 显示的应用图标（256x256，编译时经 elf2nro 嵌入 NRO）
```

## 依赖的 HA 接口

```
GET  /api/                        探活
POST /auth/login_flow             开始登录
POST /auth/login_flow/<flow_id>   提交账号密码
POST /auth/token                  换取 / 刷新令牌
POST /api/template                一次拉全量实体（名称/区域/状态/亮度/开合/单位）
GET  /api/states                  兜底（旧版 HA 没有 template API 时）
POST /api/services/<domain>/<svc> 控制
GET  /api/camera_proxy/<entity>   摄像头快照
```

## 连不上服务器？用「连接诊断」

外网地址（如 `https://ha.xxx.com`）和内网地址一样可以用，连不上时先看诊断结果：
入口在 **设置 → 连接诊断**（地址页按 X 也可以），会逐项检查并显示每一步的真实错误：

| 诊断输出 | 含义 | 处理 |
|---|---|---|
| ① 首页 连不上：Could not resolve host | 域名解析失败 | 检查 Switch 的 Wi-Fi/DNS；换 `223.5.5.5` 之类公共 DNS 试试 |
| ① 首页 连不上：SSL connect error / handshake failure | TLS 握手被拒 | 你的反代可能只开了 TLS 1.3 或强制 HTTP/2。新版会自动回退重试（IPv4 / HTTP1.1 / TLS1.2 三种组合），若仍失败，在反代上把 TLS 1.2 也打开 |
| ① HTTP 403 | 被反代/WAF 拦截 | Cloudflare 等开了「Bot 拦截」会拦掉程序化请求，把该域名加入白名单或关闭 Bot Fight Mode |
| ② API HTTP 404 | 地址不是 HA 根路径 | 地址应填到域名/端口为止，不要带 `/lovelace` 之类的子路径 |
| 三项全 200 但登录失败 | 凭据问题 | 账号开了两步验证就走「长期令牌」登录 |

> 注意：HA 的 OAuth 登录流本身支持外网域名，手机 App 走外网能登录，Switch 版同样可以。
> 唯一额外要求是 Switch 本机能上外网（NAT 路由器正常放行出站即可）。

### 外网访问（域名只有 IPv6 / 没有公网 IPv4）

家庭宽带没有公网 IPv4 时，DDNS 填出来的往往只有 AAAA（IPv6）记录。
Switch 的 homebrew 网络栈对 IPv6 支持很差，纯 AAAA 域名基本连不上，
诊断页的「⓪ 解析」会明确标出这种情况。

**根治办法：给域名一个 IPv4 入口，推荐 Cloudflare Tunnel**（免费、
不需要公网 IP、不改路由器）：

1. 注册 Cloudflare，把域名（spaceship 上买的）的 NS 服务器改到 Cloudflare
2. NAS Docker 里跑 tunnel：
   ```
   docker run -d --name cloudflared --restart unless-stopped \
     cloudflare/cloudflared:latest tunnel --no-autoupdate run \
     --token <面板里给的token>
   ```
3. Cloudflare 面板 → Zero Trust → Networks → Tunnels → 建一条隧道：
   公网主机名 `ha.example.com` → 服务 `http://192.168.1.100:8123`
4. 完成后域名会自动获得 A/AAAA 记录（Cloudflare 的入口 IP），
   Switch、手机、任何设备都能直接 `https://ha.example.com` 访问

替代方案：把域名托管到 Cloudflare 后只加 AAAA 记录并**开橙云代理**，
Cloudflare 会给域名配 IPv4 入口并回源到你的 IPv6——但源站必须公网可达，
不如 Tunnel 简单安全。

配好之后：地址 1 填内网、地址 2 填 `https://ha.example.com`，
家里走内网、出门自动走外网，一个 NRO 两头都能用。

### DNS 解析失败（`Couldn't resolve host name`）

Switch 系统自带的 DNS resolver 是简化实现，部分域名（CNAME 链 / DNSSEC 签名 /
运营商 DNS 不回应）会直接解析失败。新版做了三层兜底：

1. **自动 DoH**：系统解析失败时自动改用 DoH（阿里 223.5.5.5 → Cloudflare
   1.1.1.1 → 腾讯 1.12.12.12，IP 直连不再依赖本机 DNS），解析成功自动重试并缓存
2. **手动填 IP**：地址页第二个输入框「服务器 IP」。填 NAS 的局域网 IP
   （如 `192.168.1.100`），域名照旧用于 Host 头和 TLS SNI——反向代理按域名
   分流不受影响。**内网穿透 / 反代隐藏端口的场景推荐直接填内网 IP**，
   还能顺便避开路由器 NAT 回环问题
3. 诊断页的「⓪ 解析」一行会显示最终解析结果与来源
