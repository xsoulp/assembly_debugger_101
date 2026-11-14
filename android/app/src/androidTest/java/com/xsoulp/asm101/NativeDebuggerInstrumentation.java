package com.xsoulp.asm101;

import android.app.Activity;
import android.app.Instrumentation;
import android.os.Bundle;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/** Runs real hardware tests inside the app's ordinary, unprivileged Android UID. */
public final class NativeDebuggerInstrumentation extends Instrumentation {
    private int assertions;
    @Override public void onCreate(Bundle args) { super.onCreate(args); start(); }
    private void check(boolean condition,String message) {
        ++assertions;
        if (!condition) throw new AssertionError(message);
    }
    private JSONObject state(String json) throws Exception { return new JSONObject(json); }
    private long reg(JSONObject s,int index) throws Exception {
        return Long.parseUnsignedLong(s.getJSONArray("registers").getString(index).substring(2),16);
    }
    private int variable(JSONObject s,String name) throws Exception {
        JSONArray rows=s.getJSONArray("memory");
        for (int i=0;i<rows.length();++i) {
            JSONObject row=rows.getJSONObject(i);
            JSONArray labels=row.getJSONArray("labels");
            for (int n=0;n<labels.length();++n)
                if (name.equals(labels.getJSONObject(n).getString("name")))
                    return row.getJSONArray("bytes").getInt(labels.getJSONObject(n).getInt("offset"));
        }
        throw new AssertionError("Missing variable: "+name);
    }
    private String example(String name) throws Exception {
        try (InputStream input=getTargetContext().getAssets().open("examples/"+name)) {
            ByteArrayOutputStream output=new ByteArrayOutputStream();
            byte[] buffer=new byte[4096]; int count;
            while ((count=input.read(buffer))!=-1) output.write(buffer,0,count);
            return new String(output.toByteArray(),StandardCharsets.UTF_8);
        }
    }
    @Override public void onStart() {
        Bundle result=new Bundle();
        try {
            check(android.os.Process.myUid()>=10000,"Tests must run as the app UID, not root/shell");
            state(NativeBridge.build(example("variables-stack.s")));
            JSONObject s=state(NativeBridge.start());
            check(s.getBoolean("active"),"Child did not stop at entry");
            check(s.getInt("pid")>0,"No real debuggee PID");
            check(reg(s,0)==0,"Initial X0 must be cleared");
            long sp=reg(s,31), pc=reg(s,32);
            check((sp&15)==0,"SP must be 16-byte aligned");
            s=state(NativeBridge.step()); // ADR
            check(reg(s,32)==pc+4,"PC did not advance one instruction");
            s=state(NativeBridge.step()); // LDR
            check(reg(s,0)==10,"LDR did not read the real variable");
            s=state(NativeBridge.step()); // ADD
            check(reg(s,0)==15,"ADD did not change X0");
            s=state(NativeBridge.step()); // STR
            check(variable(s,"value")==15,"STR did not write real RAM");
            s=state(NativeBridge.step()); // MOV
            check(reg(s,1)==15,"Register copy failed");
            s=state(NativeBridge.step()); // PUSH
            check(reg(s,31)==sp-16,"Push did not decrement real SP");
            s=state(NativeBridge.step()); // Stack load
            check(reg(s,2)==15,"Stack read failed");
            s=state(NativeBridge.step()); // POP
            check(reg(s,3)==15&&reg(s,31)==sp,"Pop did not restore SP and X3");
            s=state(NativeBridge.step()); // ADR result
            s=state(NativeBridge.step()); // STR result
            check(variable(s,"result")==15,"Final result in RAM is wrong");
            s=state(NativeBridge.step()); // NOP completes
            check(!s.getBoolean("active"),"Child remains active after end of source");
            check(variable(s,"result")==15,"Final RAM was not preserved");
            s=state(NativeBridge.start());
            check(variable(s,"value")==10&&variable(s,"result")==0,"Restart did not reset data");
            state(NativeBridge.stop());
            for (String name:new String[]{"loop-flags.s","call-stack.s"}) {
                state(NativeBridge.build(example(name))); s=state(NativeBridge.start());
                int steps=0;
                while (s.getBoolean("active")&&steps++<100) s=state(NativeBridge.step());
                check(!s.getBoolean("active"),name+" did not complete");
                check(variable(s,name.startsWith("loop")?"sum":"result")==15,name+" produced wrong result");
                if (name.startsWith("loop")) check((reg(s,33)&(1L<<30))!=0,"Z flag was not set");
            }
            state(NativeBridge.build(".text\n_start:\nmov x9, #0\nldr x0, [x9]\nnop\n"));
            state(NativeBridge.start()); state(NativeBridge.step()); s=state(NativeBridge.step());
            check(!s.getBoolean("active")&&s.getString("status").contains("signal 11"),"SIGSEGV was not reported");
            state(NativeBridge.build(".text\n_start:\nmov x0, #0\nmov x8, #93\nsvc #0\n"));
            s=state(NativeBridge.start());
            while(s.getBoolean("active")) s=state(NativeBridge.step());
            check(s.getString("status").contains("exited with code 0"),"Native exit syscall failed");
            state(NativeBridge.build(".text\n_start: b _start\n"));
            s=state(NativeBridge.start()); pc=reg(s,32);
            s=state(NativeBridge.step()); check(reg(s,32)==pc,"Loop branch did not execute on hardware");
            s=state(NativeBridge.stop()); check(!s.getBoolean("active"),"Stop left a child running");
            state(NativeBridge.build(".text\n_start: brk #1\nnop\n"));
            state(NativeBridge.start()); s=state(NativeBridge.step());
            check(!s.getBoolean("active")&&s.getString("status").contains("breakpoint"),"BRK was not reported");
            result.putString("stream","PASS: "+assertions+" native ARM64 assertions; variables, registers, stack, flags, calls, restart, faults and exit.\n");
            finish(Activity.RESULT_OK,result);
        } catch (Throwable e) {
            try { NativeBridge.stop(); } catch (Throwable ignored) {}
            result.putString("stream","FAIL: "+e+"\n");
            finish(Activity.RESULT_CANCELED,result);
        }
    }
}
