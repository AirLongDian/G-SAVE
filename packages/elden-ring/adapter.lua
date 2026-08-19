local PROCESS_NAME = "eldenring.exe"
local STEAM_APP_ID = 1245620
local EXECUTABLE_RELATIVE_PATH = "Game/eldenring.exe"
local BND4_HEADER_SIZE = 64
local BND4_ENTRY_HEADER_SIZE = 32
local PROFILE_ENTRY_INDEX = 10
local PROFILE_ACTIVE_OFFSET = 0x1954
local PROFILE_BASE_OFFSET = 0x195E
local PROFILE_STRIDE = 0x24C
local PROFILE_NAME_BYTES = 32
local SLOT_COUNT = 10
local MAX_PROFILE_ENTRY_SIZE = 1024 * 1024

local function problem(code, message)
    return { code = code, message = message }
end

local function normalize(path)
    return path:gsub("\\", "/")
end

local function is_account_name(name)
    return name:match("^%d+$") ~= nil
end

local function u32le(bytes, offset)
    if #bytes < offset + 3 then
        return nil
    end
    local a, b, c, d = bytes:byte(offset, offset + 3)
    return a | (b << 8) | (c << 16) | (d << 24)
end

local function u64le(bytes, offset)
    if #bytes < offset + 7 then
        return nil
    end
    local low = u32le(bytes, offset)
    local high = u32le(bytes, offset + 4)
    return low | (high << 32)
end

local function utf16le_name(bytes, offset, byte_length)
    local result = {}
    local limit = offset + byte_length - 1
    local cursor = offset
    while cursor + 1 <= limit do
        local low, high = bytes:byte(cursor, cursor + 1)
        local codepoint = low | (high << 8)
        if codepoint == 0 then
            break
        end
        if codepoint >= 0xD800 and codepoint <= 0xDBFF and cursor + 3 <= limit then
            local next_low, next_high = bytes:byte(cursor + 2, cursor + 3)
            local next_codepoint = next_low | (next_high << 8)
            if next_codepoint < 0xDC00 or next_codepoint > 0xDFFF then
                return nil
            end
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10)
                + (next_codepoint - 0xDC00)
            cursor = cursor + 2
        elseif codepoint >= 0xDC00 and codepoint <= 0xDFFF then
            return nil
        end
        result[#result + 1] = utf8.char(codepoint)
        cursor = cursor + 2
    end
    return table.concat(result)
end

local function save_identity(path)
    local normalized = normalize(path)
    local account = normalized:match("^(%d+)/[Ee][Rr]0000%.[Ss][Ll]2$")
    if account then
        return account, "original", "原版"
    end
    account = normalized:match("^(%d+)/[Ee][Rr]0000%.[Cc][Oo]2$")
    if account then
        return account, "seamless_coop", "无缝联机"
    end
    return nil, nil, nil
end

local function parse_save(repository, original_path, account, save_kind, save_label, warnings)
    local path = normalize(original_path)
    local stat = repository:stat(original_path) or {}
    local header = repository:read(original_path, 0, BND4_HEADER_SIZE) or ""
    if #header ~= BND4_HEADER_SIZE or header:sub(1, 4) ~= "BND4" then
        warnings[#warnings + 1] = {
            code = "invalid_save_header",
            path = path,
            message = "文件不是受支持的 ELDEN RING BND4 存档",
        }
        return {
            account_id = account .. "（" .. save_label .. "）",
            steam_id = account,
            save_kind = save_kind,
            path = path,
            valid = false,
            slots = {},
            file_size = stat.size,
            modified_unix_ns = stat.modified_unix_ns,
        }
    end

    local entry_count = u32le(header, 13)
    if entry_count == nil or entry_count <= PROFILE_ENTRY_INDEX then
        warnings[#warnings + 1] = {
            code = "missing_profile_entry",
            path = path,
            message = "BND4 缺少 USER_DATA_010，无法读取角色槽位",
        }
        return {
            account_id = account .. "（" .. save_label .. "）",
            steam_id = account,
            save_kind = save_kind,
            path = path,
            valid = false,
            slots = {},
            file_size = stat.size,
            modified_unix_ns = stat.modified_unix_ns,
        }
    end

    local entry_header_offset = BND4_HEADER_SIZE
        + PROFILE_ENTRY_INDEX * BND4_ENTRY_HEADER_SIZE
    local entry_header = repository:read(
        original_path, entry_header_offset, BND4_ENTRY_HEADER_SIZE) or ""
    local entry_size = u32le(entry_header, 9)
    local entry_offset = u32le(entry_header, 17)
    local minimum_payload = PROFILE_BASE_OFFSET + PROFILE_STRIDE * SLOT_COUNT
    if #entry_header ~= BND4_ENTRY_HEADER_SIZE
        or entry_header:sub(1, 8) ~= string.char(0x50, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF)
        or entry_size == nil or entry_size < 16 + minimum_payload
        or entry_size > MAX_PROFILE_ENTRY_SIZE
        or entry_offset == nil or entry_offset < BND4_HEADER_SIZE
        or (stat.size ~= nil and entry_offset + entry_size > stat.size) then
        warnings[#warnings + 1] = {
            code = "invalid_profile_entry",
            path = path,
            message = "USER_DATA_010 的数据范围无效",
        }
        return {
            account_id = account .. "（" .. save_label .. "）",
            steam_id = account,
            save_kind = save_kind,
            path = path,
            valid = false,
            slots = {},
            file_size = stat.size,
            modified_unix_ns = stat.modified_unix_ns,
        }
    end

    local entry = repository:read(original_path, entry_offset, entry_size) or ""
    if #entry ~= entry_size or repository:md5(entry:sub(17)) ~= entry:sub(1, 16) then
        warnings[#warnings + 1] = {
            code = "profile_checksum_mismatch",
            path = path,
            message = "USER_DATA_010 的 MD5 完整性校验失败",
        }
        return {
            account_id = account .. "（" .. save_label .. "）",
            steam_id = account,
            save_kind = save_kind,
            path = path,
            valid = false,
            slots = {},
            file_size = stat.size,
            modified_unix_ns = stat.modified_unix_ns,
        }
    end

    local payload = entry:sub(17)
    local embedded_steam_id = u64le(payload, 5)
    local slots = {}
    for slot_index = 0, SLOT_COUNT - 1 do
        local profile_offset = PROFILE_BASE_OFFSET + slot_index * PROFILE_STRIDE + 1
        local name = utf16le_name(payload, profile_offset, PROFILE_NAME_BYTES)
        local active = payload:byte(PROFILE_ACTIVE_OFFSET + slot_index + 1) ~= 0
        if name == nil then
            warnings[#warnings + 1] = {
                code = "invalid_character_name",
                path = path,
                slot = slot_index + 1,
                message = "角色名不是有效 UTF-16LE",
            }
        end
        local occupied = active or (name ~= nil and name ~= "")
        slots[#slots + 1] = {
            index = slot_index + 1,
            occupied = occupied,
            character_name = name or "",
            level = u32le(payload, profile_offset + 34),
            played_seconds = u32le(payload, profile_offset + 38),
        }
    end

    return {
        account_id = account .. "（" .. save_label .. "）",
        steam_id = embedded_steam_id and tostring(embedded_steam_id) or account,
        save_kind = save_kind,
        path = path,
        valid = true,
        container = "BND4",
        container_entry_count = entry_count,
        slots = slots,
        file_size = stat.size,
        modified_unix_ns = stat.modified_unix_ns,
    }
end

function install(context)
    local problems = {}
    local repositories = {}
    local process_path = context.steam_executable(STEAM_APP_ID, EXECUTABLE_RELATIVE_PATH)
    if process_path == nil then
        problems[#problems + 1] = problem(
            "game_executable_not_found",
            "未在 Steam 库中找到 ELDEN RING，请确认游戏已通过 Steam 安装")
    end

    local roaming = context.known_folder("roaming_app_data")
    local save_root = roaming and context.path_join(roaming, "EldenRing") or nil
    local has_save = false
    if save_root and context.is_directory(save_root) then
        for _, account_path in ipairs(context.list_directories(save_root)) do
            local account = context.basename(account_path)
            if is_account_name(account)
                and (context.is_file(context.path_join(account_path, "ER0000.sl2"))
                    or context.is_file(context.path_join(account_path, "ER0000.co2"))) then
                has_save = true
                break
            end
        end
    end

    if has_save then
        repositories[1] = {
            path = save_root,
            include_globs = { "*/ER0000.sl2", "*/ER0000.co2" },
            exclude_globs = { ".git/**", "GraphicsConfig.xml", "**/*.bak" },
        }
    else
        problems[#problems + 1] = problem(
            "save_not_found",
            "未找到有效的 EldenRing/<SteamID>/ER0000.sl2 或 ER0000.co2")
    end

    return {
        process_name = PROCESS_NAME,
        process_path = process_path,
        repositories = repositories,
        problems = problems,
    }
end

function parse(repository, changed_files)
    local accounts = {}
    local warnings = {}
    local changed = {}
    for _, path in ipairs(changed_files or {}) do
        changed[#changed + 1] = normalize(path)
    end
    table.sort(changed)

    for _, original_path in ipairs(repository:files()) do
        local account, save_kind, save_label = save_identity(original_path)
        if account then
            accounts[#accounts + 1] = parse_save(
                repository, original_path, account, save_kind, save_label, warnings)
        end
    end
    table.sort(accounts, function(left, right)
        if left.steam_id == right.steam_id then
            return left.save_kind < right.save_kind
        end
        return left.steam_id < right.steam_id
    end)

    return {
        game_id = "elden-ring",
        repository = repository.path,
        accounts = accounts,
        changed_files = changed,
        warnings = warnings,
    }
end
