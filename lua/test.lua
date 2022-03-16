
return function()
    print "hello co11"
    luvco.spawn_local(function()
        print "hello co21"
        print "hello co22"
        luvco.spawn_local(function()
            print "hello co3"
            luvco.spawn_local(function()
                print "hello co4"
            end)
        end)
    end)
    collectgarbage("collect")
    print "hello co12"
end