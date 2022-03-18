* lua_pcall 保证捕获运行函数的错误，但如果传入的参数数量不对，
    则lua_pcall自身运行错误，程序会卡住

* Lua C function 返回int代表返回值的数量，返回n代表栈顶往下n个
    为返回值，栈底的多余值丢。lua_pcall 的返回值参数代表截取
    多少返回值，这里由前向后截取，优先截取原先栈底方向的。

* lua_yieldk 会保留栈，resume后能继续使用;