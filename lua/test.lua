local addr = luvco.net.new_ip4_addr("127.0.0.1", 8080)
local server = luvco_net.new_server(addr)

while true do
    local connection = server:accept()
    if connection == nil then
        break
    end

    print ("lua: accpet return", connection)
    luvco.spawn_local(function()
        repeat
            local reads = connection:read()
            local write_ret = connection:write(reads)
            if string.sub(reads, 1, 5) == "close" then
                server:close();
            end
        until string.sub(reads, 1, 4) == "quit"
        connection:close()
    end)
end

server:close()