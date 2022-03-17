
return function()
    print "hello co11"
    local addr = net.new_ip4_addr("127.0.0.1", 8080)
    local server = net.new_server(addr)
    local connection = server:accept()
end