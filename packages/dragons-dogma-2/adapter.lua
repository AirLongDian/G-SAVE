local PROCESS_NAME = "DD2.exe"
local STEAM_APP_ID = 2054970

local function normalize(path)
    return path:gsub("\\", "/")
end

local function account_from_path(path)
    return normalize(path):match("/userdata/([^/]+)/2054970/remote/win64_save$") or "Steam"
end

function install(context)
    local process_path = context.steam_executable(STEAM_APP_ID, "DD2.exe")
    local repositories = {}
    for _, remote in ipairs(context.steam_userdata(STEAM_APP_ID)) do
        local save_root = context.path_join(remote, "win64_save")
        if context.is_directory(save_root)
            and (context.is_file(context.path_join(save_root, "data000.bin"))
                or context.is_file(context.path_join(save_root, "data001Slot.bin"))) then
            repositories[#repositories + 1] = {
                path = save_root,
                include_globs = { "*.bin" },
                exclude_globs = { ".git/**" },
            }
        end
    end
    local problems = {}
    if process_path == nil then
        problems[#problems + 1] = {
            code = "game_executable_not_found",
            message = "未在 Steam 库中找到 DRAGON'S DOGMA 2，请确认游戏已通过 Steam 安装",
        }
    end
    if #repositories == 0 then
        problems[#problems + 1] = {
            code = "save_not_found",
            message = "未找到 Steam userdata/<账号>/2054970/remote/win64_save",
        }
    end
    return {
        process_name = PROCESS_NAME,
        process_path = process_path,
        repositories = repositories,
        problems = problems,
    }
end

function parse(repository, changed_files)
    local present = {}
    for _, file in ipairs(repository:files()) do
        present[normalize(file):lower()] = file
    end
    local recent = present["data000.bin"]
    local inn = present["data001slot.bin"]
    local recent_stat = recent and repository:stat(recent) or {}
    local inn_stat = inn and repository:stat(inn) or {}
    local changed = {}
    for _, file in ipairs(changed_files or {}) do
        changed[#changed + 1] = normalize(file)
    end
    table.sort(changed)
    return {
        game_id = "dragons-dogma-2",
        accounts = {{
            account_id = account_from_path(repository.path),
            slots = {
                {
                    index = 1,
                    occupied = recent ~= nil,
                    character_name = "",
                    label = "最近存档",
                    path = recent,
                    file_size = recent_stat.size,
                    modified_unix_ns = recent_stat.modified_unix_ns,
                },
                {
                    index = 2,
                    occupied = inn ~= nil,
                    character_name = "",
                    label = "旅店存档",
                    path = inn,
                    file_size = inn_stat.size,
                    modified_unix_ns = inn_stat.modified_unix_ns,
                },
            },
        }},
        changed_files = changed,
    }
end
