luvco.import("net")
luvco.import("chan")
luvco.import("lualibs")

local sender = luvco.ispawn_s(
    --function ()
    [[
        luvco.import("net")
        luvco.import("lualibs")
        local ok, data = recv_parent:recv()
        print ("recv over", ok, data)
    ]]
    --end
)

local addr = luvco.net.new_ip4_addr("127.0.0.1", 8080)
local server = luvco.net.new_server(addr)

local ok = sender:send(addr)
print ("send over", ok)

server:close()
print ("close server")