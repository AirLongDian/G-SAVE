local PROCESS_NAME = "DDDA.exe"
local STEAM_APP_ID = 367500

local function normalize(path)
    return path:gsub("\\", "/")
end

local function account_from_path(path)
    return normalize(path):match("/userdata/([^/]+)/367500/remote$") or "Steam"
end

function install(context)
    local process_path = context.steam_executable(STEAM_APP_ID, "DDDA.exe")
    local repositories = {}
    for _, remote in ipairs(context.steam_userdata(STEAM_APP_ID)) do
        if context.is_file(context.path_join(remote, "DDDA.sav")) then
            repositories[#repositories + 1] = {
                path = remote,
                include_globs = { "DDDA.sav", "0", "1", "2", "3", "4", "5", "6", "7" },
                exclude_globs = { ".git/**" },
            }
        end
    end
    local problems = {}
    if process_path == nil then
        problems[#problems + 1] = {
            code = "game_executable_not_found",
            message = "未在 Steam 库中找到 DRAGON'S DOGMA: DARK ARISEN，可手动选择 DDDA.exe",
        }
    end
    if #repositories == 0 then
        problems[#problems + 1] = {
            code = "save_not_found",
            message = "未找到 Steam userdata/<账号>/367500/remote/DDDA.sav",
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
    local save
    for _, file in ipairs(repository:files()) do
        if normalize(file):lower() == "ddda.sav" then
            save = file
            break
        end
    end
    local stat = save and repository:stat(save) or {}
    local changed = {}
    for _, file in ipairs(changed_files or {}) do
        changed[#changed + 1] = normalize(file)
    end
    table.sort(changed)
    return {
        game_id = "dragons-dogma-dark-arisen",
        accounts = {{
            account_id = account_from_path(repository.path),
            slots = {{
                index = 1,
                occupied = save ~= nil,
                character_name = "",
                label = "当前进度",
            }},
            path = save,
            file_size = stat.size,
            modified_unix_ns = stat.modified_unix_ns,
        }},
        changed_files = changed,
    }
end
