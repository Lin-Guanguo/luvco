
return function()
    print "hello co11"
    luvco.spawn_local(function()
        do
            local addr = luvco.net.new_ip4_addr("127.0.0.1", 8080)
            local server = luvco_net.new_server(addr)
            local connection = server:accept()
            local reads = connection:read()
            connection:close()
            server:close()
            print( reads )
        end
        collectgarbage("collect");
        luvco._free_co()
    end)
end