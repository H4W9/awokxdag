// Run the firmware's Home navigation control flow with UI mocks. No firmware
// build. Covers the paginated tiles, direct Files/Settings access, the About
// overlay, and standardized paging labels/hitboxes (plan phase 6).
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const read=name=>readFileSync(new URL(`../../AWOKxDAG/${name}`,import.meta.url),'utf8');
const main=read('AWOKxDAG.ino'), input=read('input.ino');
function block(source,start){
  assert.ok(start>=0);let end=source.indexOf('{',start)+1,depth=1;
  while(depth){if(source[end]==='{')depth++;if(source[end]==='}')depth--;end++;}
  return source.slice(start,end);
}
function fn(source,name){return block(source,source.search(new RegExp(`(?:void|bool|int) ${name}\\(`)));}
// Keep the #else branch of Mini blocks and drop Mini-only code so the host runs
// the Touch (ILI9341) path, exactly like the shipped non-Mini build.
function stripMini(s){
  return s.replace(/#ifdef AWOK_MINI_DISPLAY[\s\S]*?#else\n/g,'')
          .replace(/#ifdef AWOK_MINI_DISPLAY[\s\S]*?#endif\n/g,'')
          .replace(/#endif\n/g,'');
}
function adapt(s){return stripMini(s)
  .replace(/^(?:void|bool|int) (\w+)\(([^)]*)\)/gm,(_,name,args)=>`function ${name}(${args.replace(/const String& |bool |int /g,'')})`)
  .replace(/const (?:bool|int) /g,'const ').replace(/const HomeTile& /g,'const ')
  .replace(/\b(?:int|String) (\w+)\s*=/g,'let $1 =').replaceAll('View::','View.')
  .replace('(kHomeTileCount + kHomeTilesPerPage - 1) / kHomeTilesPerPage',
           'Math.floor((kHomeTileCount + kHomeTilesPerPage - 1) / kHomeTilesPerPage)');}

// Tie the test to the real tile table and paging constants in the source.
const tableBody=main.slice(main.indexOf('const HomeTile kHomeTiles[]'));
const tiles=[...tableBody.slice(0,tableBody.indexOf('};')).matchAll(/\{"([^"]+)",\s*(\w+),\s*(\w+)\}/g)]
  .map(m=>({label:m[1],color:m[2],action:m[3]}));
const actionNames=[...main.match(/enum HomeAction\s*\{([^}]+)\}/)[1].matchAll(/(\w+)/g)].map(m=>m[1]);
const num=name=>{const m=main.match(new RegExp(`const int ${name} = (\\d+);`));return m?Number(m[1]):null;};

function harness(){
  const calls=[],buttons=[],footers=[];let header=[];
  const targets={drawReconMenu:'kRecon',drawAttacksMenu:'kAttacks',openMonitorMenu:'kMonitor',
    drawGps:'kGps',openFilesManager:'kFiles',openSettings:'kSettings',drawStatus:'kStatus'};
  const c=vm.createContext({String,max:Math.max,min:Math.min,
    homePage:0,homeOverlay:false,reconCategory:0,reconPage:9,sdReady:true,
    currentView:'kHome',View:{kHome:'kHome',kScreenTest:'kScreenTest'},
    kScreenWidth:240,kFooterTop:278,
    kHomeTilesPerPage:num('kHomeTilesPerPage'),kHomeFirstY:num('kHomeFirstY'),
    kHomeRowPitch:num('kHomeRowPitch'),kHomeTileHeight:num('kHomeTileHeight'),
    kHomeTileCount:tiles.length,kHomeTiles:tiles,kVersion:'1.7.3',
    kBackground:0,kMuted:1,kAccent:2,kBad:3,
    kHomeRecon:'kHomeRecon',kHomeAttacks:'kHomeAttacks',kHomeMonitor:'kHomeMonitor',
    kHomeGps:'kHomeGps',kHomeFiles:'kHomeFiles',kHomeSettings:'kHomeSettings',
    kHomeStatus:'kHomeStatus',kHomeAbout:'kHomeAbout',
    finishScreenTest(){},networkToolsOpen:()=>false,closeNetworkTools(){},
    signalMonitorActive:false,
    drawHeader:(...a)=>{header=a;},drawButton:(...a)=>buttons.push(a),
    drawFooter:(l,r)=>footers.push([l,r]),
    drawAboutPage(){calls.push(['about']);},
    display:{fillScreen(){buttons.length=footers.length=0;header=[];}}});
  for(const [name,view] of Object.entries(targets))
    c[name]=()=>{calls.push([name]);c.currentView=view;};
  assert.deepEqual(actionNames,['kHomeRecon','kHomeAttacks','kHomeMonitor','kHomeGps',
    'kHomeFiles','kHomeSettings','kHomeStatus','kHomeAbout']);
  const names=['homePageCount','launchHomeTile','openAbout','drawHome'];
  vm.runInContext(adapt(names.map(n=>fn(main,n)).join('\n')),c);
  const home=block(input,input.indexOf('if (currentView == View::kHome)'));
  vm.runInContext(adapt('function homeTap(x, y) {\n'+home+'\n}'),c);
  return {c,calls,buttons,footers,header:()=>header,
    tap:(x,y)=>{c.currentView='kHome';c.homeTap(x,y);},
    labels:()=>buttons.map(b=>b[4])};
}

test('page 1 shows the first five destinations with a version/Next footer and page badge',()=>{
  const t=harness();t.c.drawHome();
  assert.deepEqual(t.labels(),['Recon','Attacks','Monitor','GPS','Files']);
  assert.equal(t.c.homePageCount(),2);assert.match(t.header()[1],/1\/2/);
  assert.deepEqual(t.footers.at(-1),['1.7.3','Next']);
  t.c.sdReady=false;t.c.drawHome();assert.match(t.header()[1],/SD missing/);
});
test('page 2 exposes Settings, Status and About with a Prev/version footer',()=>{
  const t=harness();t.c.homePage=1;t.c.drawHome();
  assert.deepEqual(t.labels(),['Settings','Status','About']);
  assert.match(t.header()[1],/2\/2/);assert.deepEqual(t.footers.at(-1),['Prev','1.7.3']);
});
test('Files and Settings are reachable directly without the Status screen',()=>{
  const t=harness();t.c.drawHome();
  t.tap(120,220);assert.equal(t.calls.at(-1)[0],'openFilesManager');   // Files, page 1
  t.c.homePage=1;t.c.drawHome();
  t.tap(120,44);assert.equal(t.calls.at(-1)[0],'openSettings');         // Settings, page 2
});
test('every destination and the recon reset route from its tile',()=>{
  const expected=['drawReconMenu','drawAttacksMenu','openMonitorMenu','drawGps','openFilesManager'];
  for(let row=0;row<5;row++){
    const t=harness();t.c.drawHome();t.tap(120,44+row*44);
    assert.equal(t.calls.at(-1)[0],expected[row]);
  }
  const t=harness();t.c.drawHome();t.tap(120,44);
  assert.equal(t.c.reconCategory,-1);assert.equal(t.c.reconPage,0);   // recon resets to picker
  t.c.homePage=1;t.c.drawHome();t.tap(120,88);assert.equal(t.calls.at(-1)[0],'drawStatus');
});
test('About opens an overlay that any tap dismisses back to page 1',()=>{
  const t=harness();t.c.homePage=1;t.c.drawHome();
  t.tap(120,132);assert.equal(t.c.homeOverlay,true);assert.equal(t.calls.at(-1)[0],'about');
  t.tap(5,5);assert.equal(t.c.homeOverlay,false);assert.equal(t.c.homePage,0);
  assert.deepEqual(t.labels(),['Recon','Attacks','Monitor','GPS','Files']);
});
test('paging is bounded and the version slots are inert',()=>{
  const t=harness();t.c.drawHome();
  t.tap(30,300);assert.equal(t.c.homePage,0);        // version slot (left) does nothing
  t.tap(200,300);assert.equal(t.c.homePage,1);       // Next advances
  t.tap(200,300);assert.equal(t.c.homePage,1);       // version slot (right) on last page
  t.tap(30,300);assert.equal(t.c.homePage,0);        // Prev returns
});
test('gaps between tiles and the dead strip above the footer launch nothing',()=>{
  const t=harness();t.c.drawHome();const before=t.calls.length;
  for(const y of [43,84,264,270,277])t.tap(120,y);
  assert.equal(t.calls.length,before);assert.equal(t.c.currentView,'kHome');
});
