
return function()
    print "hello co11"
    luvco.spawn_local(function()
        do
            local addr = luvco.net.new_ip4_addr("127.0.0.1", 8080)
            local server = luvco_net.new_server(addr)
            local connection = server:accept()

            repeat
                local reads = connection:read()
                print( reads )
                local write_ret = connection:write( "hello echo=\"", reads, "\"\n")
                print( "write ret", write_ret)
            until string.sub(reads, 1, 4) == "quit"

            connection:close()
            server:close()
        end
        collectgarbage("collect");
        luvco._free_co()
    end)
end