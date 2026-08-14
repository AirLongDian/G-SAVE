local source_root = assert(arg[1], "source root argument is required"):gsub("\\", "/")
local sl2_path = assert(arg[2], "DS30000.sl2 path argument is required"):gsub("\\", "/")
local account_id = arg[3] or sl2_path:match("/([0-9a-fA-F]+)/[^/]+$")
    or "0110000000000000"
local relative_path = account_id .. "/DS30000.sl2"

install = nil
parse = nil
dofile(source_root .. "/packages/dark-souls-iii/adapter.lua")
assert(type(parse) == "function", "adapter must export parse")

local repository = { path = sl2_path:match("^(.*)/[^/]+$") }
function repository:files()
    return { relative_path }
end
function repository:read(relative, offset, length)
    assert(relative == relative_path)
    local file = assert(io.open(sl2_path, "rb"))
    assert(file:seek("set", offset))
    local value = file:read(length)
    file:close()
    return value
end
function repository:stat(relative)
    assert(relative == relative_path)
    local file = assert(io.open(sl2_path, "rb"))
    local size = assert(file:seek("end"))
    file:close()
    return { size = size }
end
function repository:aes_128_cbc_decrypt(ciphertext, key, iv)
    return native_aes_128_cbc_decrypt(ciphertext, key, iv)
end
function repository:md5(bytes)
    return native_md5(bytes)
end

local metadata = parse(repository, { relative_path })
assert(#metadata.accounts == 1, "expected exactly one DS3 account")
local account = metadata.accounts[1]
assert(account.valid, "sample is not a valid DS3 BND4 save")
assert(#account.slots == 10, "DS3 parser must always output ten slots")

print("account_directory=" .. account.account_id)
print("steam_id=" .. tostring(account.steam_id))
print("bnd4_entries=" .. tostring(account.container_entry_count))
for _, slot in ipairs(account.slots) do
    print(string.format(
        "slot=%d occupied=%s name=%s",
        slot.index,
        tostring(slot.occupied),
        slot.character_name))
end
for _, warning in ipairs(metadata.warnings) do
    print(string.format(
        "warning=%s slot=%s message=%s",
        warning.code,
        tostring(warning.slot or ""),
        warning.message))
end
