const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert');
const root = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
for (const [, asset] of html.matchAll(/(?:href|src)="(static\/[^\"]+)"/g)) {
    assert(fs.existsSync(path.join(root, asset)), `缺少网页资源：${asset}`);
}
const solidCssPath = path.join(root, 'static/vendor/fontawesome/css/solid.min.css');
const solidCss = fs.readFileSync(solidCssPath, 'utf8');
for (const [, asset] of solidCss.matchAll(/url\(([^)]+)\)/g)) {
    assert(fs.existsSync(path.resolve(path.dirname(solidCssPath), asset)),
        `缺少图标字体：${asset}`);
}
for (const script of html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/g)) {
    if (script[1].trim()) new vm.Script(script[1]);
}
function section(start, end) {
    const first = html.indexOf(start);
    const last = html.indexOf(end, first + start.length);
    assert(first >= 0 && last > first, `找不到网页脚本区间：${start} → ${end}`);
    return html.slice(first, last);
}
const core = section('const FIVE_LEVEL_MAP =', 'function refreshResults()');
const io = section('function parseINI(text)', 'let autosaveTimer');
const context = { TextEncoder, state: { sets: [], precision: 5, autosave: true } };
vm.createContext(context);
vm.runInContext(core + '\n' + io, context);
const sample = fs.readFileSync(path.join(root, 'tests', 'fixtures', 'sample.ini'), 'utf8');
const parsed = context.parseINI(sample);
const firstCourse = 'c1=高等数学|4.0|95|percent';
for (const credit of ['1.234', '1e2', '-1', '.']) {
    assert.throws(() => context.parseINI(sample.replace(firstCourse,
        `c1=高等数学|${credit}|95|percent`)));
}
for (const [credit, expected] of [['.5', 0.5], ['1.', 1]]) {
    const ini = sample.replace(firstCourse, `c1=高等数学|${credit}|95|percent`);
    assert.equal(context.parseINI(ini).sets[0].courses[0].credit, expected);
}
const alternateIndexes = sample.replace('sets=2', 'sets=+02')
    .replace('[set1]', '[set01]')
    .replace('c1=高等数学', 'c01=高等数学');
assert.equal(context.parseINI(alternateIndexes).sets[0].courses[0].credit, 4);
assert.throws(() => context.parseINI(sample.replace('c2=大学物理', 'course=大学物理')));
assert.throws(() => context.parseINI(sample + ';' + 'x'.repeat(1022)));
assert.equal(parsed.sets.length, 2);
assert.equal(parsed.sets[0].courses.length, 4);
assert.equal(parsed.precision, 5);
const result = context.calcSet(parsed.sets[0]);
assert.equal(result.count, 4);
assert.equal(result.percentCount, 2);
assert.equal(result.gpaCount, 3);
assert(Math.abs(result.credit - 10) < 1e-8);
assert(Math.abs(result.avg - 91.5) < 1e-8);
assert(Math.abs(result.weighted - 556 / 6) < 1e-8);
assert(Math.abs(result.gpa - (context.percentGpa(95) * 4 + 4 * 3 + context.percentGpa(88) * 2) / 9) < 1e-8);
const limited = context.calcSet({ courses: [
    { credit: 2, type: 'fivelevel', score: '不及格' },
    { credit: 3, type: 'passfail', score: '不通过' },
    { credit: 4, type: 'passfail', score: '' },
    { credit: 5, type: 'percent', score: '95junk' },
    { credit: 0, type: 'percent', score: '100' }
] });
assert.equal(limited.count, 2);
assert.equal(limited.percentCount, 0);
assert.equal(limited.gpaCount, 1);
assert.equal(limited.credit, 5);
assert.equal(limited.gpa, 0);
const passOnly = context.calcSet({ courses: [
    { credit: 3, type: 'passfail', score: '不通过' }
] });
assert.equal(passOnly.count, 1);
assert.equal(passOnly.gpaCount, 0);
assert.equal(passOnly.credit, 3);
const labels = {};
context.document = { getElementById: id => labels[id] ||= { textContent: '' } };
vm.runInContext(section('function refreshResults()', 'function setAutosave(enabled)'), context);
context.state.currentSet = 0;
context.state.sets = [{ courses: [{ credit: 3, type: 'passfail', score: '不通过' }] }];
context.refreshResults();
assert.equal(labels['r-avg'].textContent, '—');
assert.equal(labels['r-gpa'].textContent, '—');
assert.equal(labels['r-credit'].textContent, '3.00');
context.state.sets = [{ courses: [{ credit: 2, type: 'fivelevel', score: '不及格' }] }];
context.refreshResults();
assert.equal(labels['r-avg'].textContent, '—');
assert.equal(labels['r-gpa'].textContent, '0.00000');
assert.equal(context.percentValue('95junk'), -1);
assert.equal(context.percentValue(''), -1);
let historySnapshots = 0;
const editListeners = {};
context.pushUndo = () => historySnapshots++;
vm.runInContext(section('function registerEditHistory(input)',
    'function renderTabContent()'), context);
const beginEdit = context.registerEditHistory({
    addEventListener(name, callback) { editListeners[name] = callback; }
});
assert.equal(historySnapshots, 0);
editListeners.focus();
beginEdit();
beginEdit();
assert.equal(historySnapshots, 1);
editListeners.focus();
beginEdit();
assert.equal(historySnapshots, 2);
context.state.sets = parsed.sets;
context.state.precision = 0;
context.state.sets[0].courses.push({ name: '', credit: 0, score: '', type: 'percent' });
const roundtrip = context.parseINI(context.generateINI());
assert.equal(roundtrip.precision, 0);
assert.equal(roundtrip.sets[0].courses[4].credit, 0);
assert.equal(roundtrip.sets[0].courses[4].score, '');
context.state.sets[0].courses[0].credit = 1e21;
const largeCreditINI = context.generateINI();
assert.match(largeCreditINI, /高等数学\|1000000000000000000000\.00\|95\|percent/);
assert.equal(context.parseINI(largeCreditINI).sets[0].courses[0].credit, 1e21);
context.state.sets[0].courses[0].credit = 4;
assert.throws(() => context.parseINI('[general]\nsets=1\n[set1]\ncourses=1\nc999=Bad|1|95|percent\n'));
let renders = 0;
context.renderTabContent = () => renders++;
context.refreshResults = () => {};
context.scheduleAutosave = () => {};
vm.runInContext(section('function updateCourse(idx, field, value)', 'function renderAll()'), context);
context.state.currentSet = 0;
context.updateCourse(0, 'name', '微积分');
assert.equal(renders, 0);
context.updateCourse(0, 'type', 'fivelevel');
assert.equal(renders, 1);
vm.runInContext(section('function updateSettingsControls()', 'function renderTabBar()'), context);
context.state.fileHandle = null;
context.state.autosave = true;
context.updateSettingsControls();
assert.equal(labels['autosave-toggle'].disabled, true);
assert.equal(labels['autosave-toggle'].checked, false);
context.state.fileHandle = {};
context.updateSettingsControls();
assert.equal(labels['autosave-toggle'].disabled, false);
assert.equal(labels['autosave-toggle'].checked, true);
context.state.autosave = false;
context.updateSettingsControls();
assert.equal(labels['autosave-toggle'].checked, false);
let scheduled = 0;
let scheduledCallback;
let localDraft;
context.clearTimeout = () => {};
context.setTimeout = callback => {
    scheduledCallback = callback;
    return ++scheduled;
};
context.localStorage = {
    setItem(key, value) {
        assert.equal(key, 'buaa-gpa-studio');
        localDraft = value;
    }
};
context.setStatus = () => {};
vm.runInContext(section('let autosaveTimer = null;', 'async function saveINI()'), context);
vm.runInContext(section('function setAutosave(enabled)', 'function setPrecision(value)'), context);

async function checkAutosave() {
    context.state.fileHandle = null;
    context.scheduleAutosave();
    assert.equal(scheduled, 1);
    await scheduledCallback();
    assert.equal(context.parseINI(localDraft).sets[0].courses.length,
                 context.state.sets[0].courses.length);
    assert.equal(context.parseINI(localDraft).autosave, false);

    context.setAutosave(true);
    assert.equal(context.state.autosave, false);
    assert.equal(labels['autosave-toggle'].disabled, true);
    assert.equal(scheduled, 1);

    let fileContents;
    const handle = {
        createWritable: async () => ({
            write: async value => { fileContents = value; },
            close: async () => {}
        })
    };
    context.state.fileHandle = handle;
    context.setAutosave(true);
    assert.equal(scheduled, 2);
    assert.equal(context.parseINI(localDraft).autosave, true);
    await scheduledCallback();
    assert.equal(fileContents, localDraft);

    context.setAutosave(false);
    assert.equal(context.parseINI(localDraft).autosave, false);
    const previousFileContents = fileContents;
    context.scheduleAutosave();
    assert.equal(scheduled, 3);
    await scheduledCallback();
    assert.equal(fileContents, previousFileContents);

    context.renderAll = () => {};
    context.updateIniStatus = () => {};
    vm.runInContext(section('function loadFromText(', 'async function openINI()'), context);
    context.loadFromText(sample, 'sample.ini');
    context.updateSettingsControls();
    assert.equal(context.state.fileHandle, null);
    assert.equal(context.state.autosave, false);
    assert.equal(labels['autosave-toggle'].disabled, true);
    assert.equal(labels['autosave-toggle'].checked, false);
    assert.equal(context.parseINI(localDraft).sets.length, 2);
    assert.equal(context.parseINI(localDraft).autosave, false);
    context.loadFromText(sample, 'sample.ini', handle);
    context.updateSettingsControls();
    assert.equal(context.state.autosave, true);
    assert.equal(labels['autosave-toggle'].disabled, false);
    assert.equal(labels['autosave-toggle'].checked, true);
    assert.equal(context.parseINI(localDraft).autosave, true);

    context.state.fileHandle = null;
    context.state.autosave = false;
    vm.runInContext(section('function deepCloneSets(', 'function updateToolbar()'), context);
    const courseCount = context.state.sets[0].courses.length;
    context.state.undoStack.push(context.deepCloneSets(context.state.sets));
    context.state.sets[0].courses.push({ name: '临时课程', credit: 1, score: '', type: 'percent' });
    context.undo();
    assert.equal(scheduled, 4);
    await scheduledCallback();
    assert.equal(context.parseINI(localDraft).sets[0].courses.length, courseCount);
    context.redo();
    assert.equal(scheduled, 5);
    await scheduledCallback();
    assert.equal(context.parseINI(localDraft).sets[0].courses.length, courseCount + 1);

    const setItem = context.localStorage.setItem;
    let status;
    context.localStorage.setItem = () => { throw new Error('blocked'); };
    context.setStatus = message => { status = message; };
    context.scheduleAutosave();
    await scheduledCallback();
    assert.match(status, /草稿保存失败/);
    context.localStorage.setItem = setItem;

    vm.runInContext(section('async function downloadDesktopRelease()',
        'function showFormula()'), context);
    context.URL = URL;
    context.DESKTOP_RELEASE = {
        repository: 'theFool-wn/BUAA-GPA-Studio',
        tag: 'v1.0.0',
        asset: 'BUAA GPA Studio.exe'
    };
    for (const assetName of ['BUAA GPA Studio.exe', 'BUAA.GPA.Studio.exe']) {
        let clicked = false;
        context.fetch = async () => ({
            ok: true,
            status: 200,
            json: async () => ({ assets: [{
                name: assetName,
                browser_download_url: `https://github.com/theFool-wn/BUAA-GPA-Studio/releases/download/v1.0.0/${encodeURIComponent(assetName)}`
            }] })
        });
        context.document = {
            body: { appendChild() {} },
            createElement: () => ({
                click() { clicked = true; },
                remove() {}
            })
        };
        await context.downloadDesktopRelease();
        assert(clicked, `Release asset should be downloadable: ${assetName}`);
    }
    console.log('web checks passed');
}
checkAutosave().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
