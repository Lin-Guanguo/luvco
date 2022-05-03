luvco.import("lualibs")
luvco.import("chan")
local net = luvco.import("net");

local code =
[[
    -- luvco.import("lualibs")
    -- local net = luvco.import("net");
    -- local ok, server = recv_parent:recv()
    -- if not ok then
    --     print ("recv Error")
    -- end

    while true do
        local connection = server:accept()
        if connection == nil then
            break
        end

        print ("lua: accpet return", connection)
        luvco.spawn(function()
            repeat
                local reads = connection:read()
                if #reads == 0 then
                    print ("read eof")
                    break
                end

                local write_ret = connection:write(reads)
                if string.sub(reads, 1, 5) == "close" then
                    server:close();
                elseif string.sub(reads, 1, 2) == "gc" then
                    collectgarbage("collect");
                end
            until string.sub(reads, 1, 4) == "quit"
            connection:close()
        end)
    end

    server:close()
]]

local code1 =  [[
    luvco.import("lualibs")
    local net = luvco.import("net");

    local addr = luvco.net.new_ip4_addr("127.0.0.1", 8080)
    local server = net.new_server(addr)
]] .. code

local code2 = [[
    luvco.import("lualibs")
    local net = luvco.import("net");

    local addr = luvco.net.new_ip4_addr("127.0.0.1", 8081)
    local server = net.new_server(addr)
]] .. code



local sender = luvco.ispawn_s(code1)
-- sender:send(server)
local sender2 = luvco.ispawn_s(code2)
-- sender2:send(server2)


