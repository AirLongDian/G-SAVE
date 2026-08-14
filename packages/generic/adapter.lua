function install(_)
    return { repositories = {}, problems = {} }
end

function parse(repository, changed_files)
    local files = {}
    for _, path in ipairs(changed_files or {}) do
        local stat = repository:stat(path) or {}
        files[#files + 1] = {
            path = path,
            file_size = stat.size or 0,
            modified_unix_ns = stat.modified_unix_ns or 0,
        }
    end
    return {
        adapter = "generic",
        changed_files = files,
    }
end
