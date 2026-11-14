package com.xsoulp.asm101;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputType;
import android.text.Spannable;
import android.text.TextWatcher;
import android.text.style.BackgroundColorSpan;
import android.text.style.ForegroundColorSpan;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.ScrollView;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public final class MainActivity extends Activity {
    // Keep ptrace requests on the exact thread which forked the debuggee.
    private static final ExecutorService WORKER = Executors.newSingleThreadExecutor(r -> {
        Thread t = new Thread(r, "asm101-ptrace"); t.setDaemon(true); return t;
    });
    private static final int OPEN = 10, SAVE = 11;
    private static final String[] EXAMPLES = {"variables-stack.s", "loop-flags.s", "call-stack.s"};
    private String sourceText = "", builtSource = "";
    private boolean light, busy, editing, wide;
    private int selectedTab;
    private JSONObject snapshot = new JSONObject();
    private final Set<Integer> changedRegisters = new HashSet<>();
    private final Set<String> changedBytes = new HashSet<>();
    private EditText editor;
    private TextView gutter, status, next, flags;
    private Button build, start, step, stop;
    private final View[] pages = new View[4];
    private LinearLayout registers, memory, stack, tabs;
    private ScrollView sourceScroller;
    private int bg, panel, ink, muted, blue, green, changed, gold, border;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        var prefs = getPreferences(MODE_PRIVATE);
        light = prefs.getBoolean("light", false);
        sourceText = prefs.getString("source", "");
        if (sourceText.isEmpty()) sourceText = example(EXAMPLES[0]);
        buildUi();
    }
    private String example(String file) {
        try (InputStream input = getAssets().open("examples/" + file)) {
            return new String(readSource(input), StandardCharsets.UTF_8);
        } catch (Exception e) { return "// " + e.getMessage(); }
    }
    private byte[] readSource(InputStream input) throws java.io.IOException {
        if (input == null) throw new java.io.IOException("Cannot open source");
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[4096];
        int count;
        while ((count = input.read(buffer)) != -1) {
            if (output.size()+count>128*1024) throw new java.io.IOException("Source exceeds 128 KiB");
            output.write(buffer,0,count);
        }
        return output.toByteArray();
    }
    private int dp(float n) { return Math.round(n * getResources().getDisplayMetrics().density); }
    private int color(String s) { return Color.parseColor(s); }
    private GradientDrawable shape(int fill, int radius) {
        GradientDrawable d = new GradientDrawable(); d.setColor(fill); d.setCornerRadius(dp(radius)); return d;
    }
    private TextView label(String value, int size, int tint) {
        TextView v = new TextView(this); v.setText(value); v.setTextSize(size); v.setTextColor(tint);
        v.setPadding(dp(12), dp(8), dp(12), dp(8)); v.setGravity(Gravity.CENTER_VERTICAL); return v;
    }
    private TextView mono(String value, int size, int tint) {
        TextView v = label(value,size,tint); v.setTypeface(Typeface.MONOSPACE); return v;
    }
    private LinearLayout column() {
        LinearLayout v = new LinearLayout(this); v.setOrientation(LinearLayout.VERTICAL); return v;
    }
    private ScrollView scroll(View content) {
        ScrollView v = new ScrollView(this); v.setFillViewport(true); v.addView(content); return v;
    }
    private LinearLayout.LayoutParams weighted() { return new LinearLayout.LayoutParams(0,-1,1); }
    private void buildUi() {
        bg = color(light ? "#EDF2F7" : "#080F16"); panel = color(light ? "#FFFFFF" : "#0D161E");
        ink = color(light ? "#17212B" : "#D2DEEB"); muted = color(light ? "#526579" : "#88A3BA");
        blue = color(light ? "#0757B7" : "#72AEF5"); green = color(light ? "#157143" : "#70CBA2");
        changed = color(light ? "#D5EFDF" : "#194630"); gold = color(light ? "#FFE89A" : "#50481B");
        border = color(light ? "#D5DFEA" : "#233747");
        getWindow().setStatusBarColor(bg); getWindow().setNavigationBarColor(bg);
        LinearLayout root = column(); root.setBackgroundColor(bg);
        root.setOnApplyWindowInsetsListener((view,insets) -> {
            view.setPadding(insets.getSystemWindowInsetLeft(),insets.getSystemWindowInsetTop(),
                insets.getSystemWindowInsetRight(),insets.getSystemWindowInsetBottom()); return insets;
        });
        setContentView(root); root.requestApplyInsets();
        LinearLayout header = new LinearLayout(this); header.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = label("ASM101",24,ink); title.setTypeface(Typeface.DEFAULT,Typeface.BOLD);
        header.addView(title);
        TextView subtitle = label("ARM64 · Native",12,green);
        header.addView(subtitle,new LinearLayout.LayoutParams(0,-2,1));
        Button menu = new Button(this); menu.setText("⋮"); menu.setTextSize(22); menu.setTextColor(ink);
        menu.setBackgroundColor(Color.TRANSPARENT); header.addView(menu,new LinearLayout.LayoutParams(dp(52),dp(48)));
        menu.setContentDescription("Open, save, examples, theme and help");
        menu.setOnFocusChangeListener((v,focused) -> v.setBackground(shape(focused?border:Color.TRANSPARENT,6)));
        menu.setOnClickListener(v -> showMenu(menu)); root.addView(header);
        LinearLayout controls = new LinearLayout(this); controls.setPadding(dp(8),0,dp(8),dp(4));
        build = control(controls,"Build",color("#0757B7")); start = control(controls,"Start",color("#086B46"));
        step = control(controls,"Step",color("#0757B7")); stop = control(controls,"Stop",color("#B9283E"));
        build.setOnClickListener(v -> { hideKeyboard(); run("build"); });
        start.setOnClickListener(v -> { hideKeyboard(); run("start"); });
        step.setOnClickListener(v -> run("step")); stop.setOnClickListener(v -> run("stop"));
        root.addView(controls);
        next = mono("Build → Start → Step",11,muted); next.setMaxLines(2); root.addView(next);
        LinearLayout source = column(); source.setBackgroundColor(panel);
        source.addView(label("SOURCE · ARM64",11,muted));
        LinearLayout codeRow = new LinearLayout(this); codeRow.setGravity(Gravity.TOP);
        gutter = mono("",13,muted); gutter.setGravity(Gravity.TOP|Gravity.END);
        gutter.setIncludeFontPadding(false); gutter.setPadding(dp(8),dp(8),dp(8),dp(8));
        codeRow.addView(gutter);
        editor = new EditText(this); editor.setTextColor(ink); editor.setTextSize(13);
        editor.setTypeface(Typeface.MONOSPACE); editor.setIncludeFontPadding(false);
        editor.setGravity(Gravity.TOP|Gravity.START); editor.setBackgroundColor(Color.TRANSPARENT);
        editor.setPadding(dp(4),dp(8),dp(16),dp(8)); editor.setMinWidth(dp(460));
        editor.setInputType(InputType.TYPE_CLASS_TEXT|InputType.TYPE_TEXT_FLAG_MULTI_LINE|InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        editor.setHorizontallyScrolling(true); editor.setText(sourceText);
        codeRow.addView(editor,new LinearLayout.LayoutParams(-2,-2));
        HorizontalScrollView horizontal = new HorizontalScrollView(this); horizontal.addView(codeRow);
        sourceScroller = scroll(horizontal);
        source.addView(sourceScroller,new LinearLayout.LayoutParams(-1,0,1)); pages[0] = source;
        editor.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s,int st,int c,int a) {}
            public void onTextChanged(CharSequence s,int st,int before,int count) {
                if (!editing) { sourceText = s.toString(); styleSource(); updateActions(); }
            }
            public void afterTextChanged(Editable e) {}
        });
        registers = column(); memory = column(); stack = column();
        registers.setBackgroundColor(panel); memory.setBackgroundColor(panel); stack.setBackgroundColor(panel);
        pages[1] = scroll(registers); pages[2] = scroll(memory); pages[3] = scroll(stack);
        wide = getResources().getConfiguration().orientation == Configuration.ORIENTATION_LANDSCAPE
            && getResources().getConfiguration().screenWidthDp >= 640;
        LinearLayout body = new LinearLayout(this); root.addView(body,new LinearLayout.LayoutParams(-1,0,1));
        LinearLayout right = column();
        if (wide) {
            body.addView(source,weighted());
            View divider = new View(this); divider.setBackgroundColor(border);
            body.addView(divider,new LinearLayout.LayoutParams(dp(6),-1));
            body.addView(right,weighted());
            if (selectedTab == 0) selectedTab = 1;
        } else body.addView(right,new LinearLayout.LayoutParams(-1,-1));
        tabs = new LinearLayout(this); right.addView(tabs);
        FrameLayout pageHolder = new FrameLayout(this); right.addView(pageHolder,new LinearLayout.LayoutParams(-1,0,1));
        String[] names = {"Source","Registers","Memory","Stack"};
        for (int i = wide ? 1 : 0; i < 4; ++i) {
            final int tab = i;
            TextView button = label(names[i],12,muted); button.setGravity(Gravity.CENTER);
            tabs.addView(button,new LinearLayout.LayoutParams(0,dp(44),1));
            button.setFocusable(true);
            pageHolder.addView(pages[i],new FrameLayout.LayoutParams(-1,-1));
            button.setOnClickListener(v -> { selectedTab = tab; selectPage(); });
            button.setOnFocusChangeListener((v,focused) -> {
                if (focused) { selectedTab=tab; selectPage(); button.setBackgroundColor(border); }
                else selectPage();
            });
        }
        status = label("Edit ARM64 source, then Build",12,muted); status.setMaxLines(3); root.addView(status);
        styleSource(); drawState(); selectPage(); updateActions();
    }
    private Button control(LinearLayout row,String title,int fill) {
        Button button = new Button(this); button.setText(title); button.setTextSize(13); button.setAllCaps(false);
        button.setTextColor(Color.WHITE); button.setBackground(shape(fill,6));
        button.setOnFocusChangeListener((v,focused) -> {
            GradientDrawable d=shape(fill,6); if (focused) d.setStroke(dp(3),blue); v.setBackground(d);
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0,dp(42),1);
        lp.setMargins(dp(3),0,dp(3),0); row.addView(button,lp); return button;
    }
    private void selectPage() {
        for (int i = wide ? 1 : 0; i < 4; ++i) pages[i].setVisibility(i == selectedTab ? View.VISIBLE : View.GONE);
        for (int i = 0; i < tabs.getChildCount(); ++i) {
            int tab = i + (wide ? 1 : 0);
            TextView button = (TextView)tabs.getChildAt(i);
            button.setTextColor(tab == selectedTab ? blue : muted);
            button.setBackgroundColor(tab == selectedTab ? panel : bg);
        }
    }
    private void hideKeyboard() {
        ((InputMethodManager)getSystemService(INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(editor.getWindowToken(),0);
        editor.clearFocus();
    }
    private void updateActions() {
        boolean active = snapshot.optBoolean("active"), built = snapshot.optBoolean("built");
        build.setEnabled(!busy&&!active); start.setEnabled(!busy&&!active&&built&&sourceText.equals(builtSource));
        step.setEnabled(!busy&&active); stop.setEnabled(!busy&&active);
        for (Button b : new Button[]{build,start,step,stop}) b.setAlpha(b.isEnabled()?1f:0.35f);
        editor.setEnabled(!busy&&!active); editor.setTextColor(ink);
    }
    private void run(String operation) {
        if (busy) return;
        busy = true; updateActions();
        final String source = sourceText;
        status.setText(switch(operation) { case "build" -> "Assembling ARM64…"; case "start" -> "Starting native child…";
            case "step" -> "Stepping one CPU instruction…"; default -> "Stopping…"; });
        WORKER.execute(() -> {
            String result, error = null;
            try {
                result = switch(operation) { case "build" -> NativeBridge.build(source); case "start" -> NativeBridge.start();
                    case "step" -> NativeBridge.step(); default -> NativeBridge.stop(); };
            } catch (Throwable e) {
                error = e.getMessage() == null ? e.toString() : e.getMessage();
                try { result = NativeBridge.stop(); } catch (Throwable ignored) { result = "{}"; }
            }
            final String state = result, failure = error;
            runOnUiThread(() -> {
                if (isDestroyed()) return;
                busy = false;
                if (failure == null && operation.equals("build")) builtSource = source;
                acceptState(state);
                if (failure != null) {
                    status.setText(failure);
                    new AlertDialog.Builder(this).setTitle("ASM101").setMessage(failure).setPositiveButton("OK",null).show();
                }
            });
        });
    }
    private void acceptState(String json) {
        try {
            JSONObject nextState = new JSONObject(json);
            changedRegisters.clear(); changedBytes.clear();
            JSONArray oldRegs = snapshot.optJSONArray("registers"), newRegs = nextState.optJSONArray("registers");
            if (oldRegs != null && newRegs != null)
                for (int i = 0; i < Math.min(oldRegs.length(),newRegs.length()); ++i)
                    if (!oldRegs.optString(i).equals(newRegs.optString(i))) changedRegisters.add(i);
            Map<String,Integer> oldBytes = flattenMemory(snapshot.optJSONArray("memory"));
            Map<String,Integer> newBytes = flattenMemory(nextState.optJSONArray("memory"));
            for (var entry : newBytes.entrySet())
                if (oldBytes.containsKey(entry.getKey()) && oldBytes.get(entry.getKey())>=0 && entry.getValue()>=0
                    && !oldBytes.get(entry.getKey()).equals(entry.getValue())) changedBytes.add(entry.getKey());
            snapshot = nextState; drawState(); styleSource(); updateActions();
            int line = snapshot.optInt("line");
            if (line>0) sourceScroller.post(() -> sourceScroller.smoothScrollTo(0,
                Math.max(0,(line-1)*editor.getLineHeight()-sourceScroller.getHeight()/2)));
        } catch (JSONException e) { status.setText("Invalid native debugger response: "+e.getMessage()); updateActions(); }
    }
    private Map<String,Integer> flattenMemory(JSONArray rows) {
        Map<String,Integer> bytes = new HashMap<>();
        if (rows != null) for (int i=0;i<rows.length();++i) {
            JSONObject row = rows.optJSONObject(i); if (row == null) continue;
            JSONArray data = row.optJSONArray("bytes"); if (data == null) continue;
            for (int b=0;b<data.length();++b) bytes.put(row.optString("address")+":"+b,data.optInt(b,-1));
        }
        return bytes;
    }
    private void drawState() {
        status.setText(snapshot.optString("status","Edit ARM64 source, then Build"));
        next.setText(snapshot.optString("instruction").isEmpty() ? "Build → Start → Step"
            : "NEXT · "+snapshot.optString("instruction"));
        registers.removeAllViews(); memory.removeAllViews(); stack.removeAllViews();
        JSONArray values = snapshot.optJSONArray("registers");
        registers.addView(label("REGISTERS · actual kernel values",11,muted));
        if (values == null || values.length() == 0) registers.addView(label("Start the program to read CPU registers.",14,muted));
        else {
            long pstate = parseHex(values.optString(33));
            flags = mono(String.format(Locale.ROOT,"N %d    Z %d    C %d    V %d",(pstate>>>31)&1,(pstate>>>30)&1,(pstate>>>29)&1,(pstate>>>28)&1),14,green);
            if (changedRegisters.contains(33)) flags.setBackgroundColor(changed);
            registers.addView(flags);
            for (int i=0;i<values.length();++i) {
                String name = i<31 ? "X"+i : i==31 ? "SP" : i==32 ? "PC" : "PSTATE";
                LinearLayout item = column();
                TextView hex = mono(name+"   "+values.optString(i),14,i==32?blue:ink);
                item.addView(hex);
                if (i < 31) {
                    long n = parseHex(values.optString(i));
                    TextView dec = mono("u64 "+Long.toUnsignedString(n)+"    W"+i+" "+Long.toUnsignedString(n&0xffffffffL),11,muted);
                    dec.setPadding(dp(12),0,dp(12),dp(6)); item.addView(dec);
                }
                if (changedRegisters.contains(i)) { item.setBackgroundColor(changed); item.setAlpha(.5f); item.animate().alpha(1).setDuration(450).start(); }
                registers.addView(item);
            }
        }
        memory.addView(label("RAM · 8 bytes per row",11,muted));
        memory.addView(label("Green = data · Blue = code · Highlight = changed byte",11,muted));
        stack.addView(label("STACK · real child-process memory",11,muted));
        stack.addView(label("Gold = SP · Green = changed byte · ?? = unreadable",11,muted));
        JSONArray rows = snapshot.optJSONArray("memory");
        if (rows == null || rows.length() == 0) {
            memory.addView(label("Build to inspect the assembled program.",14,muted));
            stack.addView(label("Start to inspect the native stack.",14,muted)); return;
        }
        // Put named variables first; code bytes follow them.
        for (String section : new String[]{".data",".text","stack"})
            for (int i=0;i<rows.length();++i) {
                JSONObject row = rows.optJSONObject(i);
                if (row != null && section.equals(row.optString("section"))) drawMemoryRow(section.equals("stack")?stack:memory,row,values);
            }
        if (values == null || values.length()==0) stack.addView(label("Start to inspect the native stack.",14,muted));
    }
    private long parseHex(String s) {
        try { return Long.parseUnsignedLong(s.startsWith("0x")?s.substring(2):s,16); }
        catch (NumberFormatException e) { return 0; }
    }
    private void drawMemoryRow(LinearLayout parent,JSONObject row,JSONArray regs) {
        String address = row.optString("address"), section = row.optString("section");
        long base = parseHex(address), sp = regs == null ? 0 : parseHex(regs.optString(31));
        boolean pointsHere = regs != null && Long.compareUnsigned(sp,base)>=0 && Long.compareUnsigned(sp,base+8)<0;
        LinearLayout item = column(); item.setPadding(0,dp(4),0,dp(5));
        StringBuilder caption = new StringBuilder(section+"  "+address);
        JSONArray labels = row.optJSONArray("labels");
        if (labels != null) for (int n=0;n<labels.length();++n) {
            JSONObject label = labels.optJSONObject(n);
            if (label != null) caption.append(" · ").append(label.optString("name"))
                .append(label.optInt("offset")==0?"":" +"+label.optInt("offset"));
        }
        if (pointsHere) caption.append("  ← SP");
        TextView title = mono(caption.toString(),11,section.equals(".data")?green:section.equals(".text")?blue:muted);
        title.setPadding(dp(12),dp(3),dp(12),dp(3));
        if (pointsHere) title.setBackgroundColor(gold); item.addView(title);
        LinearLayout cells = new LinearLayout(this); cells.setPadding(dp(8),0,dp(8),0);
        JSONArray bytes = row.optJSONArray("bytes");
        for (int i=0;i<8;++i) {
            int b = bytes == null ? -1 : bytes.optInt(i,-1);
            TextView cell = mono(b<0?"??":String.format(Locale.ROOT,"%02X",b),14,ink);
            cell.setGravity(Gravity.CENTER); cell.setPadding(0,dp(6),0,dp(6));
            if (changedBytes.contains(address+":"+i)) cell.setBackgroundColor(changed);
            else cell.setBackgroundColor(border);
            if (pointsHere && sp == base+i) cell.setTextColor(blue);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0,-2,1); lp.setMargins(dp(1),0,dp(1),0);
            cells.addView(cell,lp);
        }
        item.addView(cells); parent.addView(item);
    }
    private void styleSource() {
        if (editor == null || editing) return;
        editing = true;
        Editable s = editor.getText();
        for (ForegroundColorSpan span : s.getSpans(0,s.length(),ForegroundColorSpan.class)) s.removeSpan(span);
        for (BackgroundColorSpan span : s.getSpans(0,s.length(),BackgroundColorSpan.class)) s.removeSpan(span);
        tint(s,"(?m)^\\s*\\.[a-z]+|\\b(?:mov|movz|movk|movn|add|adds|sub|subs|ldr|str|ldrb|strb|ldrh|strh|ldur|stur|stp|ldp|adr|adrp|b|bl|br|blr|ret|cmp|cmn|cbz|cbnz|nop|brk|svc|and|ands|orr|eor)\\b",blue);
        tint(s,"\\b(?:x(?:[12]?[0-9]|30)|w(?:[12]?[0-9]|30)|sp|xzr|wzr|fp|lr)\\b",ink);
        tint(s,"#?-?\\b(?:0x[0-9a-fA-F]+|[0-9]+)\\b",light?color("#9B4400"):color("#F5BB79"));
        tint(s,"(?m)(?://|;).*$",green);
        int execution = snapshot.optInt("line"), current=1, lineStart=0;
        StringBuilder numbers = new StringBuilder();
        String text = s.toString();
        for (int i=0;i<=text.length();++i) if (i==text.length() || text.charAt(i)=='\n') {
            if (current>1) numbers.append('\n'); numbers.append(current==execution?"› ":"  ").append(current);
            if (current==execution && i>lineStart) s.setSpan(new BackgroundColorSpan(gold),lineStart,i,Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
            lineStart=i+1; ++current;
        }
        gutter.setText(numbers); editing=false;
    }
    private void tint(Editable s,String expression,int tint) {
        Matcher m = Pattern.compile(expression,Pattern.CASE_INSENSITIVE).matcher(s);
        while (m.find()) s.setSpan(new ForegroundColorSpan(tint),m.start(),m.end(),Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
    }
    private void showMenu(View anchor) {
        PopupMenu menu = new PopupMenu(this,anchor);
        boolean canEdit = !busy&&!snapshot.optBoolean("active");
        menu.getMenu().add(0,1,0,"Open .s file").setEnabled(canEdit);
        menu.getMenu().add(0,2,1,"Save source…").setEnabled(!busy);
        menu.getMenu().add(0,3,2,"Examples").setEnabled(canEdit);
        menu.getMenu().add(0,4,3,light?"Dark mode":"Light mode");
        menu.getMenu().add(0,5,4,"About native debugging");
        menu.setOnMenuItemClickListener(item -> {
            switch (item.getItemId()) {
                case 1 -> {
                    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE);
                    startActivityForResult(intent,OPEN);
                }
                case 2 -> startActivityForResult(new Intent(Intent.ACTION_CREATE_DOCUMENT).setType("text/plain")
                    .addCategory(Intent.CATEGORY_OPENABLE).putExtra(Intent.EXTRA_TITLE,"example.s"),SAVE);
                case 3 -> new AlertDialog.Builder(this).setTitle("ARM64 examples").setItems(EXAMPLES,(dialog,which) ->
                    new AlertDialog.Builder(this).setMessage("Replace the editor with "+EXAMPLES[which]+"?")
                        .setNegativeButton("Cancel",null).setPositiveButton("Load",(d,w) -> editor.setText(example(EXAMPLES[which]))).show()).show();
                case 4 -> { light=!light; buildUi(); }
                case 5 -> new AlertDialog.Builder(this).setTitle("ASM101 for Android").setMessage(
                    "Real ARM64 instructions run in a separate child process on this phone. Step uses Linux ptrace hardware single-stepping; registers and RAM come from the stopped process.\n\n"
                    +"No root, remote server or CPU emulation. This app debugs its own programs, not other apps.\n\n"
                    +"Use ARM64 syntax: X0–X30, SP, LDR/STR and STP/LDP. Keep SP 16-byte aligned. This first version includes a documented A64 assembler subset; unsupported instructions report an error.\n\n"
                    +"Code is limited to 4096 instructions and data to 4096 bytes. Memory shows all variables, the first 256 code bytes and a fixed 80-byte stack window.\n\n"
                    +"Created by Luis Pinto · @xsoulp").setPositiveButton("OK",null).show();
            }
            return true;
        }); menu.show();
    }
    @Override protected void onActivityResult(int request,int result,Intent intent) {
        super.onActivityResult(request,result,intent);
        if (result!=RESULT_OK || intent==null || intent.getData()==null) return;
        Uri uri = intent.getData();
        try {
            if (request==OPEN) {
                try (InputStream input = getContentResolver().openInputStream(uri)) {
                    byte[] bytes = readSource(input);
                    editor.setText(new String(bytes,StandardCharsets.UTF_8));
                }
            } else if (request==SAVE) {
                try (OutputStream output = getContentResolver().openOutputStream(uri,"wt")) {
                    if (output==null) throw new IllegalStateException("Cannot open destination");
                    output.write(sourceText.getBytes(StandardCharsets.UTF_8));
                }
                status.setText("Source saved");
            }
        } catch (Exception e) { new AlertDialog.Builder(this).setMessage(e.getMessage()).setPositiveButton("OK",null).show(); }
    }
    @Override public void onConfigurationChanged(Configuration config) {
        super.onConfigurationChanged(config); buildUi();
    }
    @Override protected void onPause() {
        getPreferences(MODE_PRIVATE).edit().putString("source",sourceText).putBoolean("light",light).apply();
        super.onPause();
    }
    @Override protected void onStop() {
        super.onStop();
        WORKER.execute(() -> {
            try {
                String state = NativeBridge.stop();
                runOnUiThread(() -> { if (!isDestroyed()) acceptState(state); });
            } catch (Throwable ignored) { /* A library loading error is reported by Build. */ }
        });
    }
}
