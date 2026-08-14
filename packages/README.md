# G-SAVE 游戏支持包契约（API 1）

支持包只在 GUI 安装期和按需 Repository Engine 提交期执行。Core 空闲时不初始化 Lua，也不读取
`manifest.toml`；GUI 必须把安装结果中的最终进程路径、进程名和仓库路径写入主配置。

## 文件结构

```text
packages/<game-id>/
├── manifest.toml
└── adapter.lua
```

`manifest.toml` 中的路径匹配统一使用 `/`，匹配时忽略 Windows 文件名大小写：

- `*`：单个路径段内的任意字符；
- `**`：任意层级；
- `.git/**`：无条件排除；
- `watch.include_globs` / `watch.exclude_globs`：GUI 写入 Core 轻量配置时使用；
- `git.exclude_globs`：GUI 安装时写入仓库的 `.git/info/exclude`。

磁盘导入使用 `.zip` 支持包，不直接选择清单。压缩包可以把上述文件放在根目录或
单个顶层目录中，但只能包含一份 `manifest.toml`；GUI 会在临时目录安全解压、校验后
再把完整支持包复制到主配置目录。符号链接、路径穿越、超过 512 项或解压后超过
64 MiB 的压缩包会被拒绝。

## Lua 沙箱宿主接口

适配器导出两个全局函数：

```lua
install(context) -> result
parse(repository, changed_files) -> metadata
```

安装期 `context` 只提供以下受控函数：

- `known_folder(name)`：当前只允许 `roaming_app_data`、`local_app_data`；
- `steam_executable(app_id, relative_path)`：从已安装 Steam 库定位并校验文件；
- `steam_userdata(app_id)`：列出当前 Steam 安装中各账号对应的
  `userdata/<账号>/<app_id>/remote` 目录；
- `path_join(...)`：构造规范化 Windows 路径；
- `is_file(path)`、`is_directory(path)`；
- `list_directories(path)`：只返回直接子目录的完整路径；
- `basename(path)`。

安装结果包含 `process_name`、可为空的 `process_path`、`repositories` 和
`problems`。GUI 必须展示问题并允许用户修正，但只有进程文件和至少一个存档仓库
都确认有效后才能初始化 Git 和写入主配置。

提交期 `repository` 只提供：

- `path`：仓库根目录；
- `files()`：仓库内的相对文件列表（不包含 `.git` 和符号链接）；适配器仍应只解析清单声明的存档文件；
- `read(relative_path, offset, length)`：受仓库根目录约束的只读范围读取；
- `stat(relative_path)`：返回 `size`、`modified_unix_ns`。
- `aes_128_cbc_decrypt(ciphertext, key, iv)`：调用 Windows CNG 的只读解密；仅在
  Repository Engine 沙箱内提供，不允许适配器访问任意系统密钥或凭据。
- `md5(bytes)`：计算存档格式已有的完整性校验值；只用于识别损坏数据，不用于安全签名。

适配器不得联网、启动进程、写文件或直接执行 Git。遇到未知或损坏格式时返回警告，
不能猜测角色名或修改存档。
