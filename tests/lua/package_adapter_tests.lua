local source_root = assert(arg[1], "source root argument is required"):gsub("\\", "/")

local function check(condition, message)
    if not condition then
        error(message or "check failed", 2)
    end
end

local function join(...)
    local parts = { ... }
    local result = table.concat(parts, "/"):gsub("/+", "/")
    return result:gsub("/$", "")
end

local function hex(value)
    return (value:gsub("%s+", ""):gsub("..", function(pair)
        return string.char(tonumber(pair, 16))
    end))
end

local function le32(value)
    return string.char(
        value & 0xFF,
        (value >> 8) & 0xFF,
        (value >> 16) & 0xFF,
        (value >> 24) & 0xFF)
end

local function le64(value)
    return le32(value & 0xFFFFFFFF) .. le32((value >> 32) & 0xFFFFFFFF)
end

local function replace_at(value, zero_based_offset, replacement)
    return value:sub(1, zero_based_offset) .. replacement
        .. value:sub(zero_based_offset + #replacement + 1)
end

local function utf16le_ascii(value)
    return (value:gsub(".", "%0\0"))
end

local function make_context(options)
    local context = {}
    function context.known_folder(name)
        return options.known_folders[name]
    end
    function context.steam_executable(app_id, relative)
        local item = options.steam[app_id]
        if item and item.relative == relative then
            return item.path
        end
        return nil
    end
    function context.steam_userdata(app_id)
        local values = options.steam_userdata and options.steam_userdata[app_id] or {}
        local copy = {}
        for index, value in ipairs(values) do copy[index] = value end
        return copy
    end
    function context.path_join(...)
        return join(...)
    end
    function context.is_file(path)
        return options.files[path] == true
    end
    function context.is_directory(path)
        return options.directories[path] ~= nil
    end
    function context.list_directories(path)
        local values = options.directories[path] or {}
        local copy = {}
        for index, value in ipairs(values) do copy[index] = value end
        return copy
    end
    function context.basename(path)
        return path:gsub("\\", "/"):match("([^/]+)$")
    end
    return context
end

local function make_repository(path, files, contents, stats, decrypted)
    local repository = { path = path }
    function repository:files()
        local result = {}
        for index, value in ipairs(files) do result[index] = value end
        return result
    end
    function repository:read(relative, offset, length)
        local value = contents[relative] or ""
        return value:sub(offset + 1, offset + length)
    end
    function repository:stat(relative)
        return stats[relative]
    end
    function repository:aes_128_cbc_decrypt(ciphertext, key, iv)
        check(#ciphertext % 16 == 0)
        check(#key == 16)
        check(#iv == 16)
        return decrypted or native_aes_128_cbc_decrypt(ciphertext, key, iv)
    end
    function repository:md5(bytes)
        return native_md5(bytes)
    end
    return repository
end

local function load_adapter(relative)
    install = nil
    parse = nil
    dofile(source_root .. "/" .. relative)
    check(type(install) == "function", relative .. " must export install")
    check(type(parse) == "function", relative .. " must export parse")
    return install, parse
end

do
    local adapter_install, adapter_parse = load_adapter("packages/generic/adapter.lua")
    local installed = adapter_install({})
    check(#installed.repositories == 0)
    check(#installed.problems == 0)
    local metadata = adapter_parse(make_repository(
        "C:/Saves/Generic", { "slot.sav" }, {},
        { ["slot.sav"] = { size = 4321, modified_unix_ns = 9876 } }),
        { "slot.sav" })
    check(metadata.adapter == "generic")
    check(#metadata.changed_files == 1)
    check(metadata.changed_files[1].path == "slot.sav")
    check(metadata.changed_files[1].file_size == 4321)
    check(metadata.changed_files[1].modified_unix_ns == 9876)
end

do
    local plaintext = hex("6bc1bee22e409f96e93d7e117393172a")
    local ciphertext = hex("7649abac8119b246cee98e9b12e9197d")
    local key = hex("2b7e151628aed2a6abf7158809cf4f3c")
    local iv = hex("000102030405060708090a0b0c0d0e0f")
    check(native_aes_128_cbc_decrypt(ciphertext, key, iv) == plaintext,
        "Windows CNG AES-128-CBC test vector failed")
    check(native_md5("abc") == hex("900150983cd24fb0d6963f7d28e17f72"),
        "Windows CNG MD5 test vector failed")
end

do
    local adapter_install, adapter_parse = load_adapter("packages/dark-souls-iii/adapter.lua")
    local root = "C:/Users/Test/AppData/Roaming/DarkSoulsIII"
    local account = root .. "/0110000100000001"
    local context = make_context({
        known_folders = { roaming_app_data = "C:/Users/Test/AppData/Roaming" },
        steam = {
            [374320] = {
                relative = "Game/DarkSoulsIII.exe",
                path = "D:/Steam/steamapps/common/DARK SOULS III/Game/DarkSoulsIII.exe",
            },
        },
        directories = {
            [root] = { account },
            [account] = {},
        },
        files = { [account .. "/DS30000.sl2"] = true },
    })
    local installed = adapter_install(context)
    check(installed.process_name == "DarkSoulsIII.exe")
    check(#installed.repositories == 1 and installed.repositories[1].path == root)
    check(#installed.problems == 0)

    local menu_size = 0x10A2 + 0x22A * 10
    local menu = string.rep("\0", menu_size)
    menu = replace_at(menu, 8, le64(0x0110000100000001))
    menu = replace_at(menu, 0x1098, "\1\1" .. string.rep("\0", 8))
    menu = replace_at(menu, 0x10A2,
        utf16le_ascii("TestKnight") .. string.rep("\0", 12))
    menu = replace_at(menu, 0x10A2 + 0x22A,
        utf16le_ascii("TestMage") .. string.rep("\0", 16))
    local padding = 16 - (#menu % 16)
    local decrypted = menu .. string.rep(string.char(padding), padding)

    local data_offset = 0x300
    local entry = string.rep("\0", 48)
    entry = native_md5(entry:sub(17)) .. entry:sub(17)
    local sl2 = string.rep("\0", data_offset + #entry)
    sl2 = replace_at(sl2, 0, "BND4")
    sl2 = replace_at(sl2, 12, le32(12))
    local menu_header_offset = 64 + 10 * 32
    local menu_header = string.char(0x50, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF)
        .. le32(#entry) .. string.rep("\0", 4) .. le32(data_offset)
        .. string.rep("\0", 12)
    sl2 = replace_at(sl2, menu_header_offset, menu_header)
    sl2 = replace_at(sl2, data_offset, entry)

    local repository = make_repository(
        root,
        { "0110000100000001/DS30000.sl2", "GraphicsConfig.xml" },
        { ["0110000100000001/DS30000.sl2"] = sl2 },
        { ["0110000100000001/DS30000.sl2"] = { size = 9307472, modified_unix_ns = 42 } },
        decrypted)
    local metadata = adapter_parse(repository, { "0110000100000001\\DS30000.sl2" })
    check(#metadata.accounts == 1)
    check(metadata.accounts[1].valid == true)
    check(metadata.accounts[1].container_entry_count == 12)
    check(metadata.accounts[1].file_size == 9307472)
    check(metadata.accounts[1].steam_id == "76561197960265729")
    check(#metadata.accounts[1].slots == 10)
    check(metadata.accounts[1].slots[1].occupied == true)
    check(metadata.accounts[1].slots[1].character_name == "TestKnight")
    check(metadata.accounts[1].slots[2].occupied == true)
    check(metadata.accounts[1].slots[2].character_name == "TestMage")
    for index = 3, 10 do
        check(metadata.accounts[1].slots[index].occupied == false)
        check(metadata.accounts[1].slots[index].character_name == "")
    end
    check(#metadata.warnings == 0)
    check(metadata.changed_files[1] == "0110000100000001/DS30000.sl2")
end

do
    local adapter_install, adapter_parse = load_adapter(
        "packages/dragons-dogma-dark-arisen/adapter.lua")
    local remote = "D:/Steam/userdata/12345/367500/remote"
    local context = make_context({
        known_folders = {},
        steam = {
            [367500] = { relative = "DDDA.exe", path = "D:/Steam/steamapps/common/DDDA/DDDA.exe" },
        },
        steam_userdata = { [367500] = { remote } },
        directories = { [remote] = {} },
        files = { [remote .. "/DDDA.sav"] = true },
    })
    local installed = adapter_install(context)
    check(installed.process_name == "DDDA.exe")
    check(#installed.repositories == 1 and installed.repositories[1].path == remote)
    check(#installed.repositories[1].include_globs == 9)
    check(#installed.problems == 0)

    local metadata = adapter_parse(make_repository(
        remote, { "DDDA.sav", "0" }, {},
        { ["DDDA.sav"] = { size = 512, modified_unix_ns = 77 } }),
        { "DDDA.sav" })
    check(metadata.game_id == "dragons-dogma-dark-arisen")
    check(metadata.accounts[1].account_id == "12345")
    check(#metadata.accounts[1].slots == 1)
    check(metadata.accounts[1].slots[1].occupied == true)
    check(metadata.accounts[1].slots[1].character_name == "")
    check(metadata.accounts[1].slots[1].label == "当前进度")
    check(metadata.accounts[1].file_size == 512)
end

do
    local adapter_install, adapter_parse = load_adapter("packages/dragons-dogma-2/adapter.lua")
    local remote = "D:/Steam/userdata/67890/2054970/remote"
    local saves = remote .. "/win64_save"
    local context = make_context({
        known_folders = {},
        steam = {
            [2054970] = { relative = "DD2.exe", path = "E:/Steam/steamapps/common/DD2/DD2.exe" },
        },
        steam_userdata = { [2054970] = { remote } },
        directories = { [remote] = { saves }, [saves] = {} },
        files = {
            [saves .. "/data000.bin"] = true,
            [saves .. "/data001Slot.bin"] = true,
        },
    })
    local installed = adapter_install(context)
    check(installed.process_name == "DD2.exe")
    check(#installed.repositories == 1 and installed.repositories[1].path == saves)
    check(installed.repositories[1].include_globs[1] == "*.bin")
    check(#installed.problems == 0)

    local metadata = adapter_parse(make_repository(
        saves, { "data000.bin", "data001Slot.bin", "data00-1.bin" }, {}, {
            ["data000.bin"] = { size = 1000, modified_unix_ns = 88 },
            ["data001Slot.bin"] = { size = 900, modified_unix_ns = 66 },
        }), { "data000.bin", "data001Slot.bin" })
    check(metadata.game_id == "dragons-dogma-2")
    check(metadata.accounts[1].account_id == "67890")
    check(#metadata.accounts[1].slots == 2)
    check(metadata.accounts[1].slots[1].label == "最近存档")
    check(metadata.accounts[1].slots[2].label == "旅店存档")
end

do
    local adapter_install, adapter_parse = load_adapter("packages/elden-ring/adapter.lua")
    local root = "C:/Users/Test/AppData/Roaming/EldenRing"
    local account = root .. "/76561198000000001"
    local context = make_context({
        known_folders = { roaming_app_data = "C:/Users/Test/AppData/Roaming" },
        steam = {
            [1245620] = {
                relative = "Game/eldenring.exe",
                path = "D:/Steam/steamapps/common/ELDEN RING/Game/eldenring.exe",
            },
        },
        directories = {
            [root] = { account },
            [account] = {},
        },
        files = { [account .. "/ER0000.co2"] = true },
    })
    local installed = adapter_install(context)
    check(installed.process_name == "eldenring.exe")
    check(#installed.repositories == 1 and installed.repositories[1].path == root)
    check(#installed.repositories[1].include_globs == 2)
    check(#installed.problems == 0)

    local profile_active_offset = 0x1954
    local profile_base_offset = 0x195E
    local profile_stride = 0x24C
    local payload_size = profile_base_offset + profile_stride * 10
    local payload = string.rep("\0", payload_size)
    payload = replace_at(payload, 4, le64(76561198000000001))
    payload = replace_at(payload, profile_active_offset, "\1\1")
    payload = replace_at(payload, profile_base_offset,
        utf16le_ascii("Tarnished") .. string.rep("\0", 14))
    payload = replace_at(payload, profile_base_offset + 34, le32(88))
    payload = replace_at(payload, profile_base_offset + 38, le32(12345))
    payload = replace_at(payload, profile_base_offset + profile_stride,
        utf16le_ascii("Seeker") .. string.rep("\0", 20))
    payload = replace_at(payload, profile_base_offset + profile_stride + 34, le32(12))
    local entry = native_md5(payload) .. payload
    local data_offset = 0x300
    local save = string.rep("\0", data_offset + #entry)
    save = replace_at(save, 0, "BND4")
    save = replace_at(save, 12, le32(12))
    local profile_header_offset = 64 + 10 * 32
    local profile_header = string.char(0x50, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF)
        .. le32(#entry) .. string.rep("\0", 4) .. le32(data_offset)
        .. string.rep("\0", 12)
    save = replace_at(save, profile_header_offset, profile_header)
    save = replace_at(save, data_offset, entry)

    local relative = "76561198000000001/ER0000.co2"
    local metadata = adapter_parse(make_repository(
        root, { relative, relative .. ".bak", "GraphicsConfig.xml" },
        { [relative] = save },
        { [relative] = { size = #save, modified_unix_ns = 99 } }),
        { "76561198000000001\\ER0000.co2" })
    check(metadata.game_id == "elden-ring")
    check(#metadata.accounts == 1)
    check(metadata.accounts[1].account_id == "76561198000000001（无缝联机）")
    check(metadata.accounts[1].steam_id == "76561198000000001")
    check(metadata.accounts[1].valid == true)
    check(#metadata.accounts[1].slots == 10)
    check(metadata.accounts[1].slots[1].occupied == true)
    check(metadata.accounts[1].slots[1].character_name == "Tarnished")
    check(metadata.accounts[1].slots[1].level == 88)
    check(metadata.accounts[1].slots[1].played_seconds == 12345)
    check(metadata.accounts[1].slots[2].occupied == true)
    check(metadata.accounts[1].slots[2].character_name == "Seeker")
    for index = 3, 10 do
        check(metadata.accounts[1].slots[index].occupied == false)
    end
    check(#metadata.warnings == 0)
    check(metadata.changed_files[1] == "76561198000000001/ER0000.co2")
end

print("package adapter contract tests passed")
