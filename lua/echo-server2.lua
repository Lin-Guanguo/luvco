luvco.import("lualibs")
luvco.import("net")
luvco.import("chan")

local addr = luvco.net.new_ip4_addr("127.0.0.1", 8080)
local server = luvco.net.new_server(addr)

while true do
    local connection = server:accept()
    if connection == nil then
        break
    end

    print ("lua: accpet return", connection)
    local recv, send = luvco.ispawn_rs(
    [[
        luvco.import("lualibs")
        luvco.import("net")
        local ok, connection = recv_parent:recv()
        if not ok then
            print("RECV ERR!!!")
            do return end
        end

        local close = false
        repeat
            local reads = connection:read()
            if #reads == 0 then
                print ("read eof")
                break
            end

            local write_ret = connection:write(reads)
            if string.sub(reads, 1, 5) == "close" then
                close = true
                break
            elseif string.sub(reads, 1, 2) == "gc" then
                collectgarbage("collect");
            end
        until string.sub(reads, 1, 4) == "quit"
        if close then
            send_parent:send(1)
        else
            send_parent:send(0)
        end
        connection:close()
    ]]
    )

    luvco.spawn(function()
        send:send(connection)
        local ok, r = recv:recv()
        if r == 1 then
            server:close()
        end
    end)
end

print("accept when close", server:accept())