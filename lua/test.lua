print "main coro start"


local addr = luvco.net.new_ip4_addr("127.0.0.1", 8080)
local server = luvco_net.new_server(addr)
while true do
    local connection = server:accept()
    print ("lua: accpet return", connection)
    luvco.spawn_local(function()
        repeat
            local reads = connection:read()
            local write_ret = connection:write( "hello echo=\"", reads, "\"\n")
        until string.sub(reads, 1, 4) == "quit"
        connection:close()
    end)
end

print "main coro end"