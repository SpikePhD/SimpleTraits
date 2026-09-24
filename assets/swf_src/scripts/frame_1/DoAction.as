// Simple Traits - transactional trait allocation UI (AS2)
//
// Same visual language as Simple Alternate Levelling's skill menu: dimmed
// full-screen backdrop, centred header, one column of six traits with -/+,
// and text-style footer buttons. All values are previews; the plugin
// validates and commits on Confirm.

Stage.scaleMode = "showAll";
Stage.align = "";

var g_traits = [];
var g_remainingPoints = 0;
var g_totalPoints = 0;
var g_closing = false;
var g_selected = 0;
var g_rows = [];          // row movie clips, index = trait id
var g_footer = [];        // [reset, confirm]
var g_labels = {};
var g_pointsTF = undefined;

// Layout on the 1280x720 stage.
var STAGE_W = 1280;
var STAGE_H = 720;
var COL_W = 660;
var COL_X = 310;
var ROW_START_Y = 222;
var ROW_H = 48;
var TRAIT_COUNT = 6;
var FOOTER_Y = 540;
var FOOTER_BTN_W = 170;
var FOOTER_BTN_H = 40;

var RESET_INDEX = 6;
var CONFIRM_INDEX = 7;

var COLOR_TITLE = 0xEDE3C4;
var COLOR_GOLD = 0xC8B878;
var COLOR_GOLD_BRIGHT = 0xF0CC5A;
var COLOR_TEXT = 0xD8D2BE;
var COLOR_WHITE = 0xFFFFFF;
var COLOR_MUTED = 0x9A937E;
var COLOR_DISABLED = 0x5E5A50;

// ---------------------------------------------------------------------------
// Plugin -> menu interface
// ---------------------------------------------------------------------------

// Argument order matches InvokeInit in src/TraitMenu.cpp.
function ST_Init(traits, totalPoints, info) {
    g_traits = traits;
    g_remainingPoints = totalPoints;
    g_totalPoints = totalPoints;
    g_closing = false;
    for (var i = 0; i < g_traits.length; i++) {
        g_traits[i].delta = 0;
    }

    g_labels = {
        level: 0, title: "Distribute Trait Points", confirmLabel: "Confirm", resetLabel: "Reset",
        levelLabel: "Level", remainingLabel: "points remaining", carryNote: "", permanentNote: "", hint: ""
    };
    if (info != undefined) {
        for (var key in g_labels) {
            if (info[key] != undefined) { g_labels[key] = info[key]; }
        }
    }

    _build();
    _select(0);
}

function ST_UpdateTrait(id, points, delta, remaining) {
    for (var i = 0; i < g_traits.length; i++) {
        if (g_traits[i].id == id) {
            g_traits[i].points = points - delta;
            g_traits[i].delta = delta;
            break;
        }
    }
    g_remainingPoints = remaining;
    _refreshAll();
}

function ST_SetClosing() {
    g_closing = true;
    _refreshAll();
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

function _fmt(size, color, align, spacing) {
    var fmt = new TextFormat();
    fmt.font = "$EverywhereMediumFont";
    fmt.size = size;
    fmt.color = color;
    fmt.align = (align != undefined) ? align : "left";
    if (spacing != undefined) { fmt.letterSpacing = spacing; }
    return fmt;
}

function _text(parent, name, depth, x, y, w, h, value, fmt) {
    parent.createTextField(name, depth, x, y, w, h);
    var tf = parent[name];
    tf.selectable = false;
    tf.setNewTextFormat(fmt);
    tf.text = value;
    tf.setTextFormat(fmt);
    return tf;
}

function _setText(tf, value, fmt) {
    tf.text = value;
    tf.setNewTextFormat(fmt);
    tf.setTextFormat(fmt);
}

function _rect(mc, x, y, w, h, color, alpha) {
    mc.beginFill(color, alpha);
    mc.moveTo(x, y); mc.lineTo(x + w, y); mc.lineTo(x + w, y + h); mc.lineTo(x, y + h); mc.lineTo(x, y);
    mc.endFill();
}

// Horizontal bar that fades out at both ends.
function _fadeBar(mc, x, y, w, h, color, peakAlpha) {
    var matrix = { matrixType: "box", x: x, y: y, w: w, h: h, r: 0 };
    mc.beginGradientFill("linear", [color, color, color], [0, peakAlpha, 0], [0, 127, 255], matrix);
    mc.moveTo(x, y); mc.lineTo(x + w, y); mc.lineTo(x + w, y + h); mc.lineTo(x, y + h); mc.lineTo(x, y);
    mc.endFill();
}

// Vertical fade from alphaTop to alphaBottom.
function _verticalFade(mc, x, y, w, h, color, alphaTop, alphaBottom) {
    var matrix = { matrixType: "box", x: x, y: y, w: w, h: h, r: Math.PI / 2 };
    mc.beginGradientFill("linear", [color, color], [alphaTop, alphaBottom], [0, 255], matrix);
    mc.moveTo(x, y); mc.lineTo(x + w, y); mc.lineTo(x + w, y + h); mc.lineTo(x, y + h); mc.lineTo(x, y);
    mc.endFill();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

function _build() {
    _root.menuMC.removeMovieClip();
    var m = _root.createEmptyMovieClip("menuMC", 10);
    g_rows = [];
    g_footer = [];

    // Backdrop: hides whatever is behind, darker at top and bottom.
    var bg = m.createEmptyMovieClip("backdrop", 1);
    _rect(bg, 0, 0, STAGE_W, STAGE_H, 0x000000, 86);
    _verticalFade(bg, 0, 0, STAGE_W, 150, 0x000000, 70, 0);
    _verticalFade(bg, 0, STAGE_H - 150, STAGE_W, 150, 0x000000, 0, 70);
    // Darker band behind the trait list, fading out towards the sides.
    _fadeBar(bg, 0, ROW_START_Y - 22, STAGE_W, FOOTER_Y - ROW_START_Y + 8, 0x000000, 75);
    bg.useHandCursor = false;
    bg.onRelease = function() {};  // swallow clicks meant for the menu behind

    var level = Number(g_labels.level);
    var levelText = (level > 0) ? (g_labels.levelLabel + " " + level).toUpperCase() : "";
    _text(m, "levelTF", 10, 0, 30, STAGE_W, 22, levelText, _fmt(13, COLOR_MUTED, "center", 4));
    _text(m, "titleTF", 11, 0, 50, STAGE_W, 38, g_labels.title, _fmt(26, COLOR_TITLE, "center"));
    g_pointsTF = _text(m, "pointsTF", 12, 0, 90, STAGE_W, 56, "", _fmt(42, COLOR_WHITE, "center"));
    _text(m, "remainingTF", 13, 0, 144, STAGE_W, 22, g_labels.remainingLabel, _fmt(14, COLOR_MUTED, "center"));
    if (g_labels.carryNote.length > 0) {
        _text(m, "carryTF", 14, 0, 164, STAGE_W, 20, g_labels.carryNote, _fmt(12, COLOR_GOLD, "center"));
    }

    var lines = m.createEmptyMovieClip("lines", 20);
    _fadeBar(lines, 160, ROW_START_Y - 26, 960, 1, COLOR_GOLD, 70);
    _fadeBar(lines, 160, FOOTER_Y - 18, 960, 1, COLOR_GOLD, 70);

    var depth = 100;
    for (var i = 0; i < g_traits.length; i++) {
        var row = m.createEmptyMovieClip("row" + i, depth++);
        row._x = COL_X;
        row._y = ROW_START_Y + i * ROW_H;
        row.traitIndex = i;
        _buildRow(row, g_traits[i]);
        g_rows[i] = row;
    }

    var footerX = Math.floor(STAGE_W / 2);
    var reset = m.createEmptyMovieClip("resetMC", depth++);
    reset._x = footerX - FOOTER_BTN_W - 20; reset._y = FOOTER_Y; reset.kind = "reset"; reset.label = g_labels.resetLabel;
    var confirm = m.createEmptyMovieClip("confirmMC", depth++);
    confirm._x = footerX + 20; confirm._y = FOOTER_Y; confirm.kind = "confirm"; confirm.label = g_labels.confirmLabel;
    g_footer = [reset, confirm];
    for (var f = 0; f < 2; f++) {
        var btn = g_footer[f];
        btn.index = RESET_INDEX + f;
        btn.createEmptyMovieClip("bg", 1);
        _text(btn, "lbl", 2, 0, 9, FOOTER_BTN_W, 24, btn.label, _fmt(17, COLOR_TEXT, "center"));
        btn.onRollOver = function() { _select(this.index); };
        btn.onRelease = function() { _activate(this.index); };
    }

    if (g_labels.permanentNote.length > 0) {
        _text(m, "permanentTF", depth++, 0, FOOTER_Y + 50, STAGE_W, 20, g_labels.permanentNote, _fmt(12, COLOR_GOLD, "center"));
    }
    if (g_labels.hint.length > 0) {
        _text(m, "hintTF", depth++, 0, FOOTER_Y + 72, STAGE_W, 20, g_labels.hint, _fmt(12, COLOR_MUTED, "center"));
    }
    _refreshAll();
}

function _buildRow(row, trait) {
    row.createEmptyMovieClip("highlight", 1);

    // Hover target for the whole row; sits below the -/+ buttons.
    var hit = row.createEmptyMovieClip("hit", 2);
    _rect(hit, -10, 0, COL_W + 20, ROW_H - 4, 0x000000, 0);
    hit.traitIndex = row.traitIndex;
    hit.useHandCursor = false;
    hit.onRollOver = function() { _select(this.traitIndex); };
    hit.onRelease = function() { _select(this.traitIndex); };

    _text(row, "nameTF", 3, 8, 4, 220, 24, trait.name, _fmt(17, COLOR_TEXT));
    _text(row, "effectTF", 4, 8, 24, 360, 18, trait.effect, _fmt(12, trait.active ? COLOR_GOLD : COLOR_DISABLED));
    _text(row, "deltaTF", 5, 420, 12, 90, 22, "", _fmt(13, COLOR_GOLD_BRIGHT, "right"));
    _text(row, "valueTF", 6, 560, 8, 56, 30, "", _fmt(18, COLOR_WHITE, "center"));

    var minus = row.createEmptyMovieClip("minusMC", 7);
    minus._x = 536; minus._y = 22; minus.sign = -1;
    var plus = row.createEmptyMovieClip("plusMC", 8);
    plus._x = 640; plus._y = 22; plus.sign = 1;
    var buttons = [minus, plus];
    for (var b = 0; b < 2; b++) {
        var btn = buttons[b];
        btn.traitIndex = row.traitIndex;
        btn.onRollOver = function() { _select(this.traitIndex); };
        btn.onRelease = function() {
            _select(this.traitIndex);
            if (this.sign > 0) { _allocate(this.traitIndex); } else { _deallocate(this.traitIndex); }
        };
    }
    _drawRow(row.traitIndex);
}

// ---------------------------------------------------------------------------
// State and rendering
// ---------------------------------------------------------------------------

function _canAdd(i) {
    return !g_closing && g_remainingPoints > 0;
}

function _canRemove(i) {
    return !g_closing && g_traits[i].delta > 0;
}

function _hasChanges() {
    for (var i = 0; i < g_traits.length; i++) {
        if (g_traits[i].delta > 0) { return true; }
    }
    return false;
}

function _footerDisabled(index) {
    if (g_closing) { return true; }
    return index == RESET_INDEX && !_hasChanges();
}

function _drawSignButton(btn, enabled, emphasised) {
    btn.clear();
    // Transparent square keeps the whole button clickable.
    btn.beginFill(0x000000, 0);
    btn.moveTo(-14, -14); btn.lineTo(14, -14); btn.lineTo(14, 14); btn.lineTo(-14, 14); btn.lineTo(-14, -14);
    btn.endFill();
    var color = enabled ? (emphasised ? COLOR_WHITE : COLOR_GOLD) : COLOR_DISABLED;
    var alpha = enabled ? 100 : 60;
    btn.lineStyle(1, color, enabled ? 70 : 40);
    var r = 11;
    // Octagon approximates a circle without relying on curveTo support.
    for (var k = 0; k <= 8; k++) {
        var angle = k * Math.PI / 4 + Math.PI / 8;
        var px = Math.cos(angle) * r; var py = Math.sin(angle) * r;
        if (k == 0) { btn.moveTo(px, py); } else { btn.lineTo(px, py); }
    }
    btn.lineStyle(2, color, alpha);
    btn.moveTo(-5, 0); btn.lineTo(5, 0);
    if (btn.sign > 0) { btn.moveTo(0, -5); btn.lineTo(0, 5); }
    btn.useHandCursor = enabled;
    btn.enabled = true;
}

function _drawRow(i) {
    var row = g_rows[i];
    if (row == undefined) { return; }
    var trait = g_traits[i];
    var selected = (i == g_selected);

    row.highlight.clear();
    if (selected && !g_closing) {
        _fadeBar(row.highlight, -10, 1, COL_W + 20, ROW_H - 6, COLOR_GOLD, 22);
        _fadeBar(row.highlight, -10, 0, COL_W + 20, 1, COLOR_GOLD, 70);
        _fadeBar(row.highlight, -10, ROW_H - 5, COL_W + 20, 1, COLOR_GOLD, 70);
    }

    _setText(row.nameTF, trait.name, _fmt(17, selected ? COLOR_WHITE : COLOR_TEXT));
    var total = trait.points + trait.delta;
    _setText(row.valueTF, String(total), _fmt(18, trait.delta > 0 ? COLOR_GOLD_BRIGHT : COLOR_WHITE, "center"));
    _setText(row.deltaTF, trait.delta > 0 ? "+" + trait.delta : "", _fmt(13, COLOR_GOLD_BRIGHT, "right"));

    _drawSignButton(row.minusMC, _canRemove(i), selected);
    _drawSignButton(row.plusMC, _canAdd(i), selected);
}

function _drawFooter(index) {
    var btn = g_footer[index - RESET_INDEX];
    var selected = (index == g_selected);
    var disabled = _footerDisabled(index);
    btn.bg.clear();
    if (selected && !disabled) {
        _fadeBar(btn.bg, -30, 0, FOOTER_BTN_W + 60, FOOTER_BTN_H, COLOR_GOLD, 30);
        _fadeBar(btn.bg, 0, FOOTER_BTN_H - 1, FOOTER_BTN_W, 2, COLOR_GOLD_BRIGHT, 100);
    } else {
        _fadeBar(btn.bg, 20, FOOTER_BTN_H - 1, FOOTER_BTN_W - 40, 1, COLOR_GOLD, disabled ? 25 : 60);
    }
    // Transparent fill keeps the button clickable across its full area.
    _rect(btn.bg, 0, 0, FOOTER_BTN_W, FOOTER_BTN_H, 0x000000, 0);
    var color = disabled ? COLOR_DISABLED : (selected ? COLOR_WHITE : (btn.kind == "confirm" ? COLOR_GOLD_BRIGHT : COLOR_TEXT));
    _setText(btn.lbl, btn.label, _fmt(17, color, "center"));
    btn.useHandCursor = !disabled;
}

function _refreshAll() {
    if (g_pointsTF != undefined) {
        _setText(g_pointsTF, String(g_remainingPoints), _fmt(42, g_remainingPoints > 0 ? COLOR_WHITE : COLOR_MUTED, "center"));
    }
    for (var i = 0; i < g_rows.length; i++) { _drawRow(i); }
    if (g_footer.length == 2) {
        _drawFooter(RESET_INDEX);
        _drawFooter(CONFIRM_INDEX);
    }
}

// ---------------------------------------------------------------------------
// Actions (menu -> plugin)
// ---------------------------------------------------------------------------

function _allocate(i) {
    if (!_canAdd(i)) { return; }
    gfx.io.GameDelegate.call("ST_OnAllocate", [g_traits[i].id]);
}

function _deallocate(i) {
    if (!_canRemove(i)) { return; }
    gfx.io.GameDelegate.call("ST_OnDeallocate", [g_traits[i].id]);
}

function _activate(index) {
    if (g_closing) { return; }
    if (index < RESET_INDEX) { _allocate(index); return; }
    if (_footerDisabled(index)) { return; }
    if (index == RESET_INDEX) {
        gfx.io.GameDelegate.call("ST_OnReset", []);
    } else if (index == CONFIRM_INDEX) {
        g_closing = true;
        _refreshAll();
        gfx.io.GameDelegate.call("ST_OnConfirm", []);
    }
}

// ---------------------------------------------------------------------------
// Selection and keyboard navigation
// ---------------------------------------------------------------------------

function _select(index) {
    if (index < 0 || index > CONFIRM_INDEX) { return; }
    var previous = g_selected;
    g_selected = index;
    if (previous < RESET_INDEX) { _drawRow(previous); } else if (g_footer.length == 2) { _drawFooter(previous); }
    if (index < RESET_INDEX) { _drawRow(index); } else if (g_footer.length == 2) { _drawFooter(index); }
}

function _navigate(code) {
    if (g_selected >= RESET_INDEX) {
        if (code == Key.LEFT || code == Key.RIGHT) { _select(g_selected == RESET_INDEX ? CONFIRM_INDEX : RESET_INDEX); }
        else if (code == Key.UP) { _select(TRAIT_COUNT - 1); }
        return;
    }
    if (code == Key.UP) { _select(Math.max(0, g_selected - 1)); }
    if (code == Key.DOWN) {
        if (g_selected == TRAIT_COUNT - 1) { _select(CONFIRM_INDEX); return; }
        _select(g_selected + 1);
    }
    if (code == Key.RIGHT) { _allocate(g_selected); }
    if (code == Key.LEFT) { _deallocate(g_selected); }
}

var g_keyListener = {};
g_keyListener.onKeyDown = function() {
    if (g_closing || g_rows.length == 0) { return; }
    var code = Key.getCode();
    if (code == Key.ESCAPE || code == 27 || code == 67) { _activate(CONFIRM_INDEX); return; }
    if (code == 82) { _activate(RESET_INDEX); return; }
    if (code == Key.ENTER || code == 13 || code == 32 || code == 187 || code == 107 || code == 61) {
        _activate(g_selected);
        return;
    }
    if (code == Key.BACKSPACE || code == 8 || code == Key.DELETEKEY || code == 46 ||
        code == 189 || code == 109 || code == 173) {
        if (g_selected < RESET_INDEX) { _deallocate(g_selected); }
        return;
    }
    if (code == Key.TAB || code == 9) {
        var direction = Key.isDown(Key.SHIFT) ? -1 : 1;
        _select((g_selected + direction + CONFIRM_INDEX + 1) % (CONFIRM_INDEX + 1));
        return;
    }
    if (code == Key.LEFT || code == Key.RIGHT || code == Key.UP || code == Key.DOWN) { _navigate(code); }
};
Key.addListener(g_keyListener);
