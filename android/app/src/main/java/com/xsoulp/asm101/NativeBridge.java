package com.xsoulp.asm101;

/** All calls must run on one persistent worker thread: ptrace tracers are threads. */
public final class NativeBridge {
    static { System.loadLibrary("asm101"); }
    private NativeBridge() {}
    public static native String build(String source);
    public static native String start();
    public static native String step();
    public static native String stop();
}
