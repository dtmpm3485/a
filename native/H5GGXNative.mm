#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <mach-o/dyld.h>
#import <CoreFoundation/CoreFoundation.h>
#import <mach/mach.h>
#import <mach/vm_region.h>
#import <mach/mach_error.h>
#import <libkern/OSCacheControl.h>

extern FloatButton* floatBtn;
extern h5ggEngine* h5gg;
extern bool g_standalone_runmode;
void initFloatButton(void (^callback)(void));

static UIWindow *H5XWindow = nil;
static id H5XController = nil;

static UIColor* H5XColor(CGFloat r, CGFloat g, CGFloat b) {
    return [UIColor colorWithRed:r/255.0 green:g/255.0 blue:b/255.0 alpha:1.0];
}
static UIFont* H5XMono(CGFloat size) {
    UIFont *f=[UIFont fontWithName:@"Menlo-Regular" size:size];
    return f ?: [UIFont systemFontOfSize:size];
}
static NSString* H5XAddr(uint64_t v) {
    return [NSString stringWithFormat:@"0x%llX", v];
}
static NSString* H5XHex32(uint32_t v) {
    return [NSString stringWithFormat:@"0x%08X", v];
}
static uint64_t H5XParseAddr(NSString *s) {
    if(!s.length) return 0;
    unsigned long long v=0;
    NSScanner *sc=[NSScanner scannerWithString:s];
    if([s.lowercaseString hasPrefix:@"0x"]) [sc scanHexLongLong:&v];
    else [sc scanUnsignedLongLong:&v];
    return (uint64_t)v;
}

@interface H5GGXNativeController : UIViewController
@property(nonatomic,strong) UIView *header;
@property(nonatomic,strong) UIView *content;
@property(nonatomic,strong) UIStackView *tabBar;
@property(nonatomic,strong) NSMutableDictionary<NSString*,UIScrollView*> *pages;
@property(nonatomic,strong) NSMutableDictionary<NSString*,UIButton*> *tabButtons;
@property(nonatomic,strong) NSString *activeTab;

@property(nonatomic,strong) UITextField *searchValue;
@property(nonatomic,strong) UISegmentedControl *searchType;
@property(nonatomic,strong) UITextField *rangeStart;
@property(nonatomic,strong) UITextField *rangeEnd;
@property(nonatomic,strong) UILabel *searchStatus;
@property(nonatomic,strong) UIStackView *resultStack;

@property(nonatomic,strong) NSMutableArray *modules;
@property(nonatomic) NSInteger selectedModule;
@property(nonatomic,strong) UIButton *moduleButton;
@property(nonatomic,strong) UILabel *moduleInfo;
@property(nonatomic,strong) UITextField *offsetField;
@property(nonatomic,strong) UISegmentedControl *armAddressMode;
@property(nonatomic,strong) UITextField *countField;
@property(nonatomic,strong) UILabel *armStatus;
@property(nonatomic,strong) UIStackView *armStack;
@property(nonatomic,strong) UITextView *hexView;
@property(nonatomic) uint64_t armStart;

@property(nonatomic,strong) NSMutableArray *watchItems;
@property(nonatomic,strong) UIStackView *watchStack;
@property(nonatomic,strong) NSMutableArray *savedItems;
@property(nonatomic,strong) UIStackView *savedStack;
@property(nonatomic,strong) NSMutableArray *patches;
@property(nonatomic,strong) UIStackView *patchStack;
@property(nonatomic,strong) NSTimer *watchTimer;
@property(nonatomic,strong) NSString *lastWriteStatus;
@end

@implementation H5GGXNativeController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor=H5XColor(10,16,25);
    self.view.layer.cornerRadius=16;
    self.view.layer.masksToBounds=YES;

    self.pages=[NSMutableDictionary dictionary];
    self.tabButtons=[NSMutableDictionary dictionary];
    self.modules=[NSMutableArray array];
    self.watchItems=[self loadArray:@"h5ggx.native.watch"];
    self.savedItems=[self loadArray:@"h5ggx.native.saved"];
    self.patches=[self loadArray:@"h5ggx.native.patches"];

    [self buildShell];
    [self buildSearchPage];
    [self buildARM64Page];
    [self buildWatchPage];
    [self buildSavedPage];
    [self buildPatchPage];
    [self switchTab:@"search"];

    self.watchTimer=[NSTimer scheduledTimerWithTimeInterval:1.0 target:self selector:@selector(tickWatch) userInfo:nil repeats:YES];
}

- (NSMutableArray*)loadArray:(NSString*)key {
    NSArray *a=[[NSUserDefaults standardUserDefaults] objectForKey:key];
    return a ? [a mutableCopy] : [NSMutableArray array];
}
- (void)saveState {
    NSUserDefaults *d=[NSUserDefaults standardUserDefaults];
    [d setObject:self.watchItems forKey:@"h5ggx.native.watch"];
    [d setObject:self.savedItems forKey:@"h5ggx.native.saved"];
    [d setObject:self.patches forKey:@"h5ggx.native.patches"];
    [d synchronize];
}

- (UILabel*)label:(NSString*)text size:(CGFloat)size color:(UIColor*)color {
    UILabel *l=[[UILabel alloc] init];
    l.text=text; l.font=[UIFont systemFontOfSize:size]; l.textColor=color ?: UIColor.whiteColor;
    l.numberOfLines=0;
    return l;
}
- (UITextField*)field:(NSString*)placeholder value:(NSString*)value {
    UITextField *f=[[UITextField alloc] init];
    f.placeholder=placeholder; f.text=value;
    f.textColor=UIColor.whiteColor;
    f.font=[UIFont monospacedDigitSystemFontOfSize:13 weight:UIFontWeightRegular];
    f.backgroundColor=H5XColor(15,24,38);
    f.layer.cornerRadius=9;
    f.layer.borderWidth=1;
    f.layer.borderColor=H5XColor(46,62,87).CGColor;
    f.leftView=[[UIView alloc] initWithFrame:CGRectMake(0,0,9,1)];
    f.leftViewMode=UITextFieldViewModeAlways;
    f.rightView=[[UIView alloc] initWithFrame:CGRectMake(0,0,9,1)];
    f.rightViewMode=UITextFieldViewModeAlways;
    [f.heightAnchor constraintEqualToConstant:42].active=YES;
    return f;
}
- (UIButton*)button:(NSString*)title action:(SEL)sel {
    UIButton *b=[UIButton buttonWithType:UIButtonTypeSystem];
    [b setTitle:title forState:UIControlStateNormal];
    [b setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    b.titleLabel.font=[UIFont systemFontOfSize:12 weight:UIFontWeightSemibold];
    b.backgroundColor=H5XColor(29,43,64);
    b.layer.cornerRadius=9;
    b.layer.borderWidth=1;
    b.layer.borderColor=H5XColor(52,72,102).CGColor;
    [b addTarget:self action:sel forControlEvents:UIControlEventTouchUpInside];
    [b.heightAnchor constraintEqualToConstant:40].active=YES;
    return b;
}
- (UIButton*)primaryButton:(NSString*)title action:(SEL)sel {
    UIButton *b=[self button:title action:sel];
    b.backgroundColor=H5XColor(49,102,232);
    b.layer.borderColor=H5XColor(89,132,232).CGColor;
    return b;
}
- (UIView*)card {
    UIView *v=[[UIView alloc] init];
    v.backgroundColor=H5XColor(14,23,36);
    v.layer.cornerRadius=11;
    v.layer.borderWidth=1;
    v.layer.borderColor=H5XColor(39,54,78).CGColor;
    return v;
}
- (UIStackView*)hstack:(NSArray*)views {
    UIStackView *s=[[UIStackView alloc] initWithArrangedSubviews:views];
    s.axis=UILayoutConstraintAxisHorizontal; s.spacing=6; s.distribution=UIStackViewDistributionFillEqually;
    return s;
}
- (UIStackView*)pageStack:(UIScrollView**)outScroll {
    UIScrollView *scroll=[[UIScrollView alloc] init];
    scroll.translatesAutoresizingMaskIntoConstraints=NO;
    scroll.alwaysBounceVertical=YES;
    scroll.keyboardDismissMode=UIScrollViewKeyboardDismissModeOnDrag;
    scroll.backgroundColor=UIColor.clearColor;

    UIStackView *stack=[[UIStackView alloc] init];
    stack.axis=UILayoutConstraintAxisVertical; stack.spacing=9; stack.translatesAutoresizingMaskIntoConstraints=NO;
    [scroll addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:10],
        [stack.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-10],
        [stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:10],
        [stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-18],
        [stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-20]
    ]];
    if(outScroll)*outScroll=scroll;
    return stack;
}
- (void)addTitle:(NSString*)title subtitle:(NSString*)subtitle to:(UIStackView*)stack {
    UILabel *t=[self label:title size:19 color:UIColor.whiteColor];
    t.font=[UIFont systemFontOfSize:19 weight:UIFontWeightBold];
    [stack addArrangedSubview:t];
    if(subtitle.length){
        UILabel *s=[self label:subtitle size:11 color:H5XColor(164,181,207)];
        [stack addArrangedSubview:s];
    }
}

- (void)buildShell {
    self.header=[[UIView alloc] init];
    self.header.translatesAutoresizingMaskIntoConstraints=NO;
    self.header.backgroundColor=H5XColor(12,20,31);
    [self.view addSubview:self.header];

    UILabel *title=[self label:@"H5GGX Native" size:17 color:UIColor.whiteColor];
    title.font=[UIFont systemFontOfSize:17 weight:UIFontWeightBold];
    title.translatesAutoresizingMaskIntoConstraints=NO;
    [self.header addSubview:title];

    UILabel *live=[self label:@"● LIVE" size:10 color:H5XColor(113,213,153)];
    live.translatesAutoresizingMaskIntoConstraints=NO;
    [self.header addSubview:live];

    UIButton *diag=[self button:@"診断" action:@selector(showDiag)];
    diag.translatesAutoresizingMaskIntoConstraints=NO;
    [self.header addSubview:diag];

    UIButton *close=[self button:@"×" action:@selector(closeWindow)];
    close.translatesAutoresizingMaskIntoConstraints=NO;
    [self.header addSubview:close];

    UIPanGestureRecognizer *pan=[[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(dragWindow:)];
    [self.header addGestureRecognizer:pan];

    self.content=[[UIView alloc] init];
    self.content.translatesAutoresizingMaskIntoConstraints=NO;
    [self.view addSubview:self.content];

    self.tabBar=[[UIStackView alloc] init];
    self.tabBar.axis=UILayoutConstraintAxisHorizontal;
    self.tabBar.distribution=UIStackViewDistributionFillEqually;
    self.tabBar.spacing=3;
    self.tabBar.translatesAutoresizingMaskIntoConstraints=NO;
    self.tabBar.backgroundColor=H5XColor(9,15,24);
    [self.view addSubview:self.tabBar];

    [NSLayoutConstraint activateConstraints:@[
        [self.header.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [self.header.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [self.header.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [self.header.heightAnchor constraintEqualToConstant:48],

        [title.leadingAnchor constraintEqualToAnchor:self.header.leadingAnchor constant:12],
        [title.centerYAnchor constraintEqualToAnchor:self.header.centerYAnchor],
        [live.leadingAnchor constraintEqualToAnchor:title.trailingAnchor constant:7],
        [live.centerYAnchor constraintEqualToAnchor:self.header.centerYAnchor],

        [close.trailingAnchor constraintEqualToAnchor:self.header.trailingAnchor constant:-7],
        [close.centerYAnchor constraintEqualToAnchor:self.header.centerYAnchor],
        [close.widthAnchor constraintEqualToConstant:38],
        [close.heightAnchor constraintEqualToConstant:32],
        [diag.trailingAnchor constraintEqualToAnchor:close.leadingAnchor constant:-5],
        [diag.centerYAnchor constraintEqualToAnchor:self.header.centerYAnchor],
        [diag.widthAnchor constraintEqualToConstant:52],
        [diag.heightAnchor constraintEqualToConstant:32],

        [self.tabBar.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:5],
        [self.tabBar.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-5],
        [self.tabBar.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-5],
        [self.tabBar.heightAnchor constraintEqualToConstant:50],

        [self.content.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [self.content.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [self.content.topAnchor constraintEqualToAnchor:self.header.bottomAnchor],
        [self.content.bottomAnchor constraintEqualToAnchor:self.tabBar.topAnchor]
    ]];

    NSArray *keys=@[@"search",@"arm64",@"watch",@"saved",@"patch"];
    NSArray *titles=@[@"検索",@"ARM64",@"監視",@"保存",@"Patch"];
    for(NSUInteger i=0;i<keys.count;i++){
        UIButton *b=[UIButton buttonWithType:UIButtonTypeSystem];
        [b setTitle:titles[i] forState:UIControlStateNormal];
        [b setTitleColor:H5XColor(159,177,205) forState:UIControlStateNormal];
        b.titleLabel.font=[UIFont systemFontOfSize:10 weight:UIFontWeightSemibold];
        b.layer.cornerRadius=8;
        b.tag=i;
        [b addTarget:self action:@selector(tabPressed:) forControlEvents:UIControlEventTouchUpInside];
        [self.tabBar addArrangedSubview:b];
        self.tabButtons[keys[i]]=b;
    }
}

- (void)installPage:(NSString*)key scroll:(UIScrollView*)scroll {
    scroll.hidden=YES;
    [self.content addSubview:scroll];
    [NSLayoutConstraint activateConstraints:@[
        [scroll.leadingAnchor constraintEqualToAnchor:self.content.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:self.content.trailingAnchor],
        [scroll.topAnchor constraintEqualToAnchor:self.content.topAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:self.content.bottomAnchor]
    ]];
    self.pages[key]=scroll;
}

- (void)buildSearchPage {
    UIScrollView *scroll=nil; UIStackView *s=[self pageStack:&scroll];
    [self addTitle:@"値検索" subtitle:@"H5GGのメモリエンジンをネイティブUIから直接操作します。" to:s];

    self.searchValue=[self field:@"例: 100" value:@""];
    [s addArrangedSubview:[self label:@"検索値" size:11 color:H5XColor(174,189,211)]];
    [s addArrangedSubview:self.searchValue];

    self.searchType=[[UISegmentedControl alloc] initWithItems:@[@"I32",@"I64",@"F32",@"F64"]];
    self.searchType.selectedSegmentIndex=0;
    [self.searchType.heightAnchor constraintEqualToConstant:36].active=YES;
    [s addArrangedSubview:self.searchType];

    self.rangeStart=[self field:@"開始" value:@"0x000000000"];
    self.rangeEnd=[self field:@"終了" value:@"0x200000000"];
    [s addArrangedSubview:self.rangeStart];
    [s addArrangedSubview:self.rangeEnd];

    [s addArrangedSubview:[self hstack:@[
        [self primaryButton:@"検索" action:@selector(doSearch)],
        [self button:@"Nearby" action:@selector(doNearby)],
        [self button:@"クリア" action:@selector(clearSearch)]
    ]]];
    [s addArrangedSubview:[self hstack:@[
        [self button:@"結果更新" action:@selector(refreshResults)],
        [self button:@"全て編集" action:@selector(editAllResults)]
    ]]];

    self.searchStatus=[self label:@"結果: 0" size:11 color:H5XColor(151,174,210)];
    [s addArrangedSubview:self.searchStatus];

    self.resultStack=[[UIStackView alloc] init];
    self.resultStack.axis=UILayoutConstraintAxisVertical; self.resultStack.spacing=7;
    [s addArrangedSubview:self.resultStack];

    [self installPage:@"search" scroll:scroll];
}

- (void)buildARM64Page {
    UIScrollView *scroll=nil; UIStackView *s=[self pageStack:&scroll];
    [self addTitle:@"ARM64 Analyzer" subtitle:@"非脱獄の同一プロセス向けMach VM書き込み。保護変更→書込→検証→命令キャッシュ更新→保護復元まで行います。" to:s];

    self.moduleButton=[self button:@"モジュールを選択" action:@selector(selectModule)];
    [s addArrangedSubview:self.moduleButton];
    [s addArrangedSubview:[self button:@"モジュール再取得" action:@selector(loadModules)]];

    self.moduleInfo=[self label:@"未取得" size:10 color:H5XColor(153,177,215)];
    self.moduleInfo.font=H5XMono(10);
    [s addArrangedSubview:self.moduleInfo];

    self.armAddressMode=[[UISegmentedControl alloc] initWithItems:@[@"iGG VA",@"RVA"]];
    self.armAddressMode.selectedSegmentIndex=0;
    [self.armAddressMode.heightAnchor constraintEqualToConstant:36].active=YES;
    [s addArrangedSubview:self.armAddressMode];
    [s addArrangedSubview:[self label:@"iGG VA: dyld slide + 入力値 / RVA: runtime base + offset" size:10 color:H5XColor(143,166,202)]];

    self.offsetField=[self field:@"iGG Offset/VA 例: 0x100123456" value:@"0x0"];
    self.countField=[self field:@"命令数" value:@"32"];
    self.countField.keyboardType=UIKeyboardTypeNumberPad;
    [s addArrangedSubview:self.offsetField];
    [s addArrangedSubview:self.countField];
    [s addArrangedSubview:[self primaryButton:@"このOffsetを読む" action:@selector(readARM64)]];

    self.armStatus=[self label:@"まだ読み込んでいません" size:10 color:H5XColor(143,166,202)];
    [s addArrangedSubview:self.armStatus];

    self.armStack=[[UIStackView alloc] init];
    self.armStack.axis=UILayoutConstraintAxisVertical; self.armStack.spacing=7;
    [s addArrangedSubview:self.armStack];

    UILabel *hexTitle=[self label:@"Hex Viewer" size:15 color:UIColor.whiteColor];
    hexTitle.font=[UIFont systemFontOfSize:15 weight:UIFontWeightBold];
    [s addArrangedSubview:hexTitle];
    [s addArrangedSubview:[self button:@"現在位置を128バイト表示" action:@selector(readHex)]];

    self.hexView=[[UITextView alloc] init];
    self.hexView.editable=NO;
    self.hexView.scrollEnabled=YES;
    self.hexView.backgroundColor=H5XColor(7,14,23);
    self.hexView.textColor=H5XColor(198,211,232);
    self.hexView.font=H5XMono(9);
    self.hexView.layer.cornerRadius=9;
    [self.hexView.heightAnchor constraintEqualToConstant:190].active=YES;
    [s addArrangedSubview:self.hexView];

    [self installPage:@"arm64" scroll:scroll];
}

- (void)buildWatchPage {
    UIScrollView *scroll=nil; UIStackView *s=[self pageStack:&scroll];
    [self addTitle:@"リアルタイム監視" subtitle:@"登録したアドレスを1秒ごとに更新します。" to:s];
    self.watchStack=[[UIStackView alloc] init];
    self.watchStack.axis=UILayoutConstraintAxisVertical; self.watchStack.spacing=7;
    [s addArrangedSubview:self.watchStack];
    [self installPage:@"watch" scroll:scroll];
    [self renderWatch];
}
- (void)buildSavedPage {
    UIScrollView *scroll=nil; UIStackView *s=[self pageStack:&scroll];
    [self addTitle:@"保存" subtitle:@"よく使うアドレスを保存して直接編集できます。" to:s];
    self.savedStack=[[UIStackView alloc] init];
    self.savedStack.axis=UILayoutConstraintAxisVertical; self.savedStack.spacing=7;
    [s addArrangedSubview:self.savedStack];
    [self installPage:@"saved" scroll:scroll];
    [self renderSaved];
}
- (void)buildPatchPage {
    UIScrollView *scroll=nil; UIStackView *s=[self pageStack:&scroll];
    [self addTitle:@"Runtime Patch" subtitle:@"ARM64の変更履歴。元命令へ個別に復元できます。" to:s];
    [s addArrangedSubview:[self button:@"全て復元" action:@selector(restoreAllPatches)]];
    self.patchStack=[[UIStackView alloc] init];
    self.patchStack.axis=UILayoutConstraintAxisVertical; self.patchStack.spacing=7;
    [s addArrangedSubview:self.patchStack];
    [self installPage:@"patch" scroll:scroll];
    [self renderPatches];
}

- (void)tabPressed:(UIButton*)sender {
    NSArray *keys=@[@"search",@"arm64",@"watch",@"saved",@"patch"];
    if(sender.tag>=0 && sender.tag<(NSInteger)keys.count) [self switchTab:keys[sender.tag]];
}
- (void)switchTab:(NSString*)key {
    self.activeTab=key;
    for(NSString *k in self.pages) self.pages[k].hidden=![k isEqualToString:key];
    for(NSString *k in self.tabButtons) {
        UIButton *b=self.tabButtons[k];
        BOOL active=[k isEqualToString:key];
        b.backgroundColor=active?H5XColor(37,67,116):UIColor.clearColor;
        [b setTitleColor:active?UIColor.whiteColor:H5XColor(159,177,205) forState:UIControlStateNormal];
    }
    if([key isEqualToString:@"arm64"] && self.modules.count==0) [self loadModules];
    if([key isEqualToString:@"watch"]) [self renderWatch];
    if([key isEqualToString:@"saved"]) [self renderSaved];
    if([key isEqualToString:@"patch"]) [self renderPatches];
}

- (void)clearStack:(UIStackView*)stack {
    NSArray *arr=[stack.arrangedSubviews copy];
    for(UIView *v in arr){[stack removeArrangedSubview:v];[v removeFromSuperview];}
}
- (UIView*)resultRow:(NSDictionary*)x index:(NSInteger)idx {
    UIView *c=[self card];
    UIStackView *v=[[UIStackView alloc] init];
    v.axis=UILayoutConstraintAxisVertical;v.spacing=5;v.translatesAutoresizingMaskIntoConstraints=NO;
    [c addSubview:v];
    [NSLayoutConstraint activateConstraints:@[
        [v.leadingAnchor constraintEqualToAnchor:c.leadingAnchor constant:9],
        [v.trailingAnchor constraintEqualToAnchor:c.trailingAnchor constant:-9],
        [v.topAnchor constraintEqualToAnchor:c.topAnchor constant:8],
        [v.bottomAnchor constraintEqualToAnchor:c.bottomAnchor constant:-8]
    ]];
    UILabel *val=[self label:x[@"value"]?:@"" size:15 color:UIColor.whiteColor];
    val.font=[UIFont systemFontOfSize:15 weight:UIFontWeightBold];
    UILabel *addr=[self label:x[@"address"]?:@"" size:10 color:H5XColor(143,171,218)];
    addr.font=[UIFont monospacedDigitSystemFontOfSize:10 weight:UIFontWeightRegular];
    [v addArrangedSubview:val];[v addArrangedSubview:addr];

    UIButton *edit=[self button:@"編集" action:@selector(editResult:)];
    UIButton *watch=[self button:@"監視" action:@selector(watchResult:)];
    UIButton *save=[self button:@"保存" action:@selector(saveResult:)];
    edit.tag=watch.tag=save.tag=idx;
    [v addArrangedSubview:[self hstack:@[edit,watch,save]]];
    return c;
}

- (NSString*)selectedType {
    NSArray *t=@[@"I32",@"I64",@"F32",@"F64"];
    NSInteger i=self.searchType.selectedSegmentIndex;
    return (i>=0&&i<(NSInteger)t.count)?t[i]:@"I32";
}
- (void)doSearch {
    if(!self.searchValue.text.length){[self alert:@"検索値を入力してください"];return;}
    [self.view endEditing:YES];
    [h5gg searchNumber:self.searchValue.text param2:[self selectedType] param3:self.rangeStart.text param4:self.rangeEnd.text];
    [self refreshResults];
}
- (void)doNearby {
    if(!self.searchValue.text.length){[self alert:@"検索値を入力してください"];return;}
    [h5gg searchNearby:self.searchValue.text param2:[self selectedType] param3:@"0x100"];
    [self refreshResults];
}
- (void)clearSearch {
    [h5gg clearResults];
    [self refreshResults];
}
- (void)refreshResults {
    NSArray *r=[h5gg getResults:100 param1:0] ?: @[];
    [self clearStack:self.resultStack];
    NSInteger i=0;
    for(NSDictionary *x in r){[self.resultStack addArrangedSubview:[self resultRow:x index:i++]];}
    self.searchStatus.text=[NSString stringWithFormat:@"結果: %ld / 表示: %lu", [h5gg getResultsCount], (unsigned long)r.count];
}
- (NSDictionary*)resultAt:(NSInteger)idx {
    NSArray *r=[h5gg getResults:100 param1:0] ?: @[];
    return (idx>=0&&idx<(NSInteger)r.count)?r[idx]:nil;
}
- (void)editResult:(UIButton*)b {
    NSDictionary *x=[self resultAt:b.tag]; if(!x)return;
    [self prompt:@"値を編集" value:x[@"value"] completion:^(NSString *v){
        [h5gg setValue:x[@"address"] param2:v param3:x[@"type"]];
        [self refreshResults];
    }];
}
- (void)watchResult:(UIButton*)b {
    NSDictionary *x=[self resultAt:b.tag]; if(!x)return;
    for(NSDictionary *e in self.watchItems)if([e[@"address"] isEqual:x[@"address"]])return;
    [self.watchItems insertObject:@{@"address":x[@"address"],@"type":x[@"type"]} atIndex:0];
    [self saveState]; [self renderWatch];
}
- (void)saveResult:(UIButton*)b {
    NSDictionary *x=[self resultAt:b.tag]; if(!x)return;
    for(NSDictionary *e in self.savedItems)if([e[@"address"] isEqual:x[@"address"]])return;
    [self.savedItems insertObject:@{@"address":x[@"address"],@"type":x[@"type"],@"value":x[@"value"]} atIndex:0];
    [self saveState]; [self renderSaved];
}
- (void)editAllResults {
    [self prompt:@"現在の検索結果を全て編集" value:@"" completion:^(NSString *v){
        [h5gg editAll:v param3:[self selectedType]];
        [self refreshResults];
    }];
}

- (void)tickWatch { if([self.activeTab isEqualToString:@"watch"])[self renderWatch]; }
- (void)renderWatch {
    if(!self.watchStack)return;
    [self clearStack:self.watchStack];
    for(NSInteger i=0;i<(NSInteger)self.watchItems.count;i++){
        NSDictionary *x=self.watchItems[i];
        NSString *v=[h5gg getValue:x[@"address"] param2:x[@"type"]] ?: @"?";
        UIView *c=[self card]; UIStackView *s=[[UIStackView alloc] init];s.axis=UILayoutConstraintAxisVertical;s.spacing=4;s.translatesAutoresizingMaskIntoConstraints=NO;[c addSubview:s];
        [NSLayoutConstraint activateConstraints:@[[s.leadingAnchor constraintEqualToAnchor:c.leadingAnchor constant:9],[s.trailingAnchor constraintEqualToAnchor:c.trailingAnchor constant:-9],[s.topAnchor constraintEqualToAnchor:c.topAnchor constant:8],[s.bottomAnchor constraintEqualToAnchor:c.bottomAnchor constant:-8]]];
        [s addArrangedSubview:[self label:v size:15 color:UIColor.whiteColor]];
        UILabel *a=[self label:x[@"address"] size:10 color:H5XColor(143,171,218)];a.font=[UIFont monospacedDigitSystemFontOfSize:10 weight:UIFontWeightRegular];[s addArrangedSubview:a];
        UIButton *del=[self button:@"解除" action:@selector(removeWatch:)];del.tag=i;[s addArrangedSubview:del];
        [self.watchStack addArrangedSubview:c];
    }
    if(self.watchItems.count==0)[self.watchStack addArrangedSubview:[self label:@"監視なし" size:12 color:H5XColor(120,139,168)]];
}
- (void)removeWatch:(UIButton*)b {
    if(b.tag>=0&&b.tag<(NSInteger)self.watchItems.count)[self.watchItems removeObjectAtIndex:b.tag];
    [self saveState];[self renderWatch];
}
- (void)renderSaved {
    if(!self.savedStack)return;
    [self clearStack:self.savedStack];
    for(NSInteger i=0;i<(NSInteger)self.savedItems.count;i++){
        NSDictionary *x=self.savedItems[i];
        NSString *cur=[h5gg getValue:x[@"address"] param2:x[@"type"]] ?: x[@"value"];
        UIView *c=[self card]; UIStackView *s=[[UIStackView alloc] init];s.axis=UILayoutConstraintAxisVertical;s.spacing=4;s.translatesAutoresizingMaskIntoConstraints=NO;[c addSubview:s];
        [NSLayoutConstraint activateConstraints:@[[s.leadingAnchor constraintEqualToAnchor:c.leadingAnchor constant:9],[s.trailingAnchor constraintEqualToAnchor:c.trailingAnchor constant:-9],[s.topAnchor constraintEqualToAnchor:c.topAnchor constant:8],[s.bottomAnchor constraintEqualToAnchor:c.bottomAnchor constant:-8]]];
        [s addArrangedSubview:[self label:cur size:15 color:UIColor.whiteColor]];
        UILabel *a=[self label:x[@"address"] size:10 color:H5XColor(143,171,218)];a.font=[UIFont monospacedDigitSystemFontOfSize:10 weight:UIFontWeightRegular];[s addArrangedSubview:a];
        UIButton *edit=[self button:@"編集" action:@selector(editSaved:)];edit.tag=i;
        UIButton *del=[self button:@"削除" action:@selector(removeSaved:)];del.tag=i;
        [s addArrangedSubview:[self hstack:@[edit,del]]];
        [self.savedStack addArrangedSubview:c];
    }
    if(self.savedItems.count==0)[self.savedStack addArrangedSubview:[self label:@"保存なし" size:12 color:H5XColor(120,139,168)]];
}
- (void)editSaved:(UIButton*)b {
    if(b.tag<0||b.tag>=(NSInteger)self.savedItems.count)return;
    NSDictionary *x=self.savedItems[b.tag];
    NSString *cur=[h5gg getValue:x[@"address"] param2:x[@"type"]] ?: @"";
    [self prompt:@"保存アドレスを編集" value:cur completion:^(NSString *v){
        [h5gg setValue:x[@"address"] param2:v param3:x[@"type"]];
        [self renderSaved];
    }];
}
- (void)removeSaved:(UIButton*)b {
    if(b.tag>=0&&b.tag<(NSInteger)self.savedItems.count)[self.savedItems removeObjectAtIndex:b.tag];
    [self saveState];[self renderSaved];
}

- (void)loadModules {
    [self.modules removeAllObjects];
    for(uint32_t i=0;i<_dyld_image_count();i++){
        const char *name=_dyld_get_image_name(i);
        const struct mach_header *hdr=_dyld_get_image_header(i);
        if(!name||!hdr)continue;
        uint64_t start=(uint64_t)hdr;
        int64_t slide=(int64_t)_dyld_get_image_vmaddr_slide(i);
        uint64_t preferred=(uint64_t)((int64_t)start-slide);
        uint64_t size=getMachoVMSize(getpid(), mach_task_self(), start);
        [self.modules addObject:@{
            @"name":[NSString stringWithUTF8String:name],
            @"start":H5XAddr(start),
            @"end":H5XAddr(start+size),
            @"slide":[NSString stringWithFormat:@"0x%llX",(uint64_t)slide],
            @"preferred":H5XAddr(preferred),
            @"index":@(i)
        }];
    }
    if(self.selectedModule>=self.modules.count)self.selectedModule=0;
    [self updateModuleUI];
}
- (NSDictionary*)currentModule {
    if(self.selectedModule<0||self.selectedModule>=(NSInteger)self.modules.count)return nil;
    return self.modules[self.selectedModule];
}
- (void)updateModuleUI {
    NSDictionary *m=[self currentModule];
    if(!m){[self.moduleButton setTitle:@"モジュールなし" forState:UIControlStateNormal];self.moduleInfo.text=@"取得できませんでした";return;}
    NSString *name=[m[@"name"] lastPathComponent];
    [self.moduleButton setTitle:[NSString stringWithFormat:@"Module: %@",name] forState:UIControlStateNormal];
    self.moduleInfo.text=[NSString stringWithFormat:@"%@\nRUNTIME %@\nSLIDE   %@\nPREF    %@\nEND     %@",m[@"name"],m[@"start"],m[@"slide"],m[@"preferred"],m[@"end"]];
}
- (void)selectModule {
    if(self.modules.count==0)[self loadModules];
    UIAlertController *a=[UIAlertController alertControllerWithTitle:@"Mach-O Module" message:@"解析するモジュールを選択" preferredStyle:UIAlertControllerStyleActionSheet];
    NSInteger limit=MIN((NSInteger)self.modules.count,40);
    for(NSInteger i=0;i<limit;i++){
        NSDictionary *m=self.modules[i];
        NSString *title=[m[@"name"] lastPathComponent];
        [a addAction:[UIAlertAction actionWithTitle:title style:UIAlertActionStyleDefault handler:^(UIAlertAction *act){self.selectedModule=i;[self updateModuleUI];}]];
    }
    [a addAction:[UIAlertAction actionWithTitle:@"キャンセル" style:UIAlertActionStyleCancel handler:nil]];
    if(a.popoverPresentationController){a.popoverPresentationController.sourceView=self.moduleButton;a.popoverPresentationController.sourceRect=self.moduleButton.bounds;}
    [self presentViewController:a animated:YES completion:nil];
}

- (NSString*)moduleOffset:(uint64_t)addr {
    for(NSDictionary *m in self.modules){
        uint64_t s=H5XParseAddr(m[@"start"]),e=H5XParseAddr(m[@"end"]);
        if(addr>=s&&addr<e)return [NSString stringWithFormat:@"%@+0x%llX",[m[@"name"] lastPathComponent],addr-s];
    }
    return H5XAddr(addr);
}
- (uint32_t)readU32:(uint64_t)a {
    NSString *v=[h5gg getValue:H5XAddr(a) param2:@"U32"];
    return (uint32_t)strtoul(v.UTF8String,NULL,10);
}
- (uint8_t)readU8:(uint64_t)a {
    NSString *v=[h5gg getValue:H5XAddr(a) param2:@"U8"];
    return (uint8_t)strtoul(v.UTF8String,NULL,10);
}
- (NSDictionary*)decodeARM64:(uint32_t)w address:(uint64_t)a {
    if(w==0xD503201F)return @{@"asm":@"NOP"};
    if(w==0xD65F03C0)return @{@"asm":@"RET"};
    if((w&0xFFFFFC1F)==0xD61F0000)return @{@"asm":[NSString stringWithFormat:@"BR X%u",(w>>5)&31]};
    if((w&0xFFFFFC1F)==0xD63F0000)return @{@"asm":[NSString stringWithFormat:@"BLR X%u",(w>>5)&31]};
    if((w&0xFC000000)==0x14000000||(w&0xFC000000)==0x94000000){
        int32_t imm=(int32_t)(w&0x03FFFFFF); if(imm&0x02000000)imm-=0x04000000;
        uint64_t t=a+(int64_t)imm*4;
        return @{@"asm":[NSString stringWithFormat:@"%@ %@",(w&0xFC000000)==0x94000000?@"BL":@"B",[self moduleOffset:t]],@"target":@(t)};
    }
    if((w&0x7E000000)==0x34000000){
        int32_t imm=(w>>5)&0x7FFFF;if(imm&0x40000)imm-=0x80000;
        uint64_t t=a+(int64_t)imm*4; uint32_t rt=w&31;
        return @{@"asm":[NSString stringWithFormat:@"%@ %@%u, %@",(w&0x01000000)?@"CBNZ":@"CBZ",(w>>31)?@"X":@"W",rt,[self moduleOffset:t]],@"target":@(t)};
    }
    if((w&0xFF000010)==0x54000000){
        int32_t imm=(w>>5)&0x7FFFF;if(imm&0x40000)imm-=0x80000;
        uint64_t t=a+(int64_t)imm*4;
        return @{@"asm":[NSString stringWithFormat:@"B.cond #%u, %@",w&15,[self moduleOffset:t]],@"target":@(t)};
    }
    if((w&0x9F000000)==0x90000000)return @{@"asm":[NSString stringWithFormat:@"ADRP X%u, ...",w&31]};
    if((w&0x7F000000)==0x11000000)return @{@"asm":@"ADD/SUB (imm)"};
    if((w&0x3B000000)==0x39000000)return @{@"asm":@"LDR/STR"};
    return @{@"asm":[NSString stringWithFormat:@".word %@",H5XHex32(w)]};
}
- (void)readARM64 {
    NSDictionary *m=[self currentModule]; if(!m){[self alert:@"モジュールを選択してください"];return;}
    uint64_t input=H5XParseAddr(self.offsetField.text);
    uint64_t start=0;
    if(self.armAddressMode.selectedSegmentIndex==0){
        uint64_t slide=H5XParseAddr(m[@"slide"]);
        start=(slide+input)&~3ULL;
    } else {
        uint64_t base=H5XParseAddr(m[@"start"]);
        start=(base+input)&~3ULL;
    }
    NSInteger count=MAX(1,MIN(128,self.countField.text.integerValue?:32));
    self.armStart=start;
    self.armStatus.text=[NSString stringWithFormat:@"開始 %@ / %@",[self moduleOffset:start],H5XAddr(start)];
    [self clearStack:self.armStack];
    for(NSInteger i=0;i<count;i++){
        uint64_t a=start+i*4; uint32_t w=[self readU32:a]; NSDictionary *d=[self decodeARM64:w address:a];
        [self.armStack addArrangedSubview:[self armRow:a word:w decoded:d]];
    }
}
- (UIView*)armRow:(uint64_t)a word:(uint32_t)w decoded:(NSDictionary*)d {
    UIView *c=[self card];
    BOOL patched=NO; for(NSDictionary *p in self.patches)if(H5XParseAddr(p[@"address"])==a){patched=YES;break;}
    if(patched)c.layer.borderColor=H5XColor(151,111,45).CGColor;
    UIStackView *s=[[UIStackView alloc] init];s.axis=UILayoutConstraintAxisVertical;s.spacing=4;s.translatesAutoresizingMaskIntoConstraints=NO;[c addSubview:s];
    [NSLayoutConstraint activateConstraints:@[[s.leadingAnchor constraintEqualToAnchor:c.leadingAnchor constant:9],[s.trailingAnchor constraintEqualToAnchor:c.trailingAnchor constant:-9],[s.topAnchor constraintEqualToAnchor:c.topAnchor constant:8],[s.bottomAnchor constraintEqualToAnchor:c.bottomAnchor constant:-8]]];
    UILabel *loc=[self label:[self moduleOffset:a] size:10 color:H5XColor(142,171,217)];loc.font=[UIFont monospacedDigitSystemFontOfSize:10 weight:UIFontWeightRegular];
    UILabel *hex=[self label:H5XHex32(w) size:10 color:H5XColor(238,193,116)];hex.font=[UIFont monospacedDigitSystemFontOfSize:10 weight:UIFontWeightRegular];
    UILabel *asmL=[self label:d[@"asm"] size:11 color:H5XColor(221,231,246)];asmL.font=H5XMono(11);
    [s addArrangedSubview:loc];[s addArrangedSubview:hex];[s addArrangedSubview:asmL];
    UIButton *nop=[self button:@"NOP" action:@selector(nopPressed:)];nop.accessibilityValue=H5XAddr(a);
    UIButton *edit=[self button:@"編集" action:@selector(editWordPressed:)];edit.accessibilityValue=H5XAddr(a);
    NSMutableArray *btns=[NSMutableArray arrayWithObjects:nop,edit,nil];
    if(patched){UIButton *restore=[self button:@"戻す" action:@selector(restorePressed:)];restore.accessibilityValue=H5XAddr(a);[btns addObject:restore];}
    if(d[@"target"]){UIButton *follow=[self button:@"追跡" action:@selector(followPressed:)];follow.accessibilityValue=[d[@"target"] stringValue];[btns addObject:follow];}
    [s addArrangedSubview:[self hstack:btns]];
    return c;
}
- (BOOL)writeWord:(uint64_t)a word:(uint32_t)w {
    self.lastWriteStatus=@"";
    if(a==0 || (a&3ULL)!=0){
        self.lastWriteStatus=@"アドレスが4バイト境界ではありません";
        return NO;
    }
    if(h5gg.targetpid!=0 && h5gg.targetpid!=getpid()){
        self.lastWriteStatus=@"iGG互換パッチは同一プロセス専用です";
        return NO;
    }

    mach_port_t port=mach_task_self();

    // iGameGod public template compatibility:
    // vm_protect(address, sizeof(data), R|W|COPY) -> vm_write -> R|X
    kern_return_t err=vm_protect(port,
                                 (vm_address_t)a,
                                 sizeof(uint32_t),
                                 false,
                                 VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY);
    if(err!=KERN_SUCCESS){
        self.lastWriteStatus=[NSString stringWithFormat:@"iGG vm_protect RW|COPY 失敗: %d (%s)",err,mach_error_string(err)];
        return NO;
    }

    // iGG's public patch routine receives memory-byte-order hex and swaps it.
    // H5GGX UI displays the normal ARM64 encoding, so convert to iGG input form
    // and perform the same CFSwapInt32 step before vm_write.
    uint32_t iggInput=CFSwapInt32(w);
    uint32_t data=CFSwapInt32(iggInput);

    err=vm_write(port,
                 (vm_address_t)a,
                 (vm_offset_t)&data,
                 sizeof(data));
    if(err!=KERN_SUCCESS){
        vm_protect(port,(vm_address_t)a,sizeof(data),false,VM_PROT_READ|VM_PROT_EXECUTE);
        self.lastWriteStatus=[NSString stringWithFormat:@"iGG vm_write失敗: %d (%s)",err,mach_error_string(err)];
        return NO;
    }

    sys_icache_invalidate((void*)a,sizeof(data));

    kern_return_t restoreErr=vm_protect(port,
                                        (vm_address_t)a,
                                        sizeof(data),
                                        false,
                                        VM_PROT_READ|VM_PROT_EXECUTE);

    uint32_t verify=[self readU32:a];
    if(verify!=w){
        self.lastWriteStatus=[NSString stringWithFormat:@"iGG書込検証失敗: expected=%@ got=%@",H5XHex32(w),H5XHex32(verify)];
        return NO;
    }
    if(restoreErr!=KERN_SUCCESS){
        self.lastWriteStatus=[NSString stringWithFormat:@"iGG書込成功 / RX復元失敗: %d (%s)",restoreErr,mach_error_string(restoreErr)];
        return YES;
    }

    self.lastWriteStatus=[NSString stringWithFormat:@"iGG PATCH OK  %@",H5XHex32(w)];
    return YES;
}
- (void)applyPatch:(uint64_t)a newWord:(uint32_t)nw {
    uint32_t old=[self readU32:a];
    if(![self writeWord:a word:nw]){[self alert:[NSString stringWithFormat:@"ランタイム書き込みに失敗しました\n\n%@",self.lastWriteStatus?:@"不明なエラー"]];return;}
    self.armStatus.text=[NSString stringWithFormat:@"PATCH OK  %@  →  %@",[self moduleOffset:a],H5XHex32(nw)];
    NSMutableDictionary *found=nil;
    for(NSMutableDictionary *p in self.patches)if(H5XParseAddr(p[@"address"])==a){found=p;break;}
    if(found)found[@"current"]=@(nw);
    else [self.patches insertObject:[@{@"address":H5XAddr(a),@"original":@(old),@"current":@(nw)} mutableCopy] atIndex:0];
    [self saveState];[self renderPatches];[self readARM64];
}
- (void)nopPressed:(UIButton*)b {[self applyPatch:H5XParseAddr(b.accessibilityValue) newWord:0xD503201F];}
- (void)editWordPressed:(UIButton*)b {
    uint64_t a=H5XParseAddr(b.accessibilityValue); uint32_t w=[self readU32:a];
    [self prompt:@"ARM64 U32 HEX (8桁)" value:[H5XHex32(w) substringFromIndex:2] completion:^(NSString *v){
        NSScanner *sc=[NSScanner scannerWithString:v]; unsigned int n=0;
        if(v.length!=8||![sc scanHexInt:&n]){[self alert:@"8桁の16進数で入力してください"];return;}
        [self applyPatch:a newWord:n];
    }];
}
- (void)restorePressed:(UIButton*)b {[self restoreAddress:H5XParseAddr(b.accessibilityValue)];}
- (void)followPressed:(UIButton*)b {
    uint64_t target=(uint64_t)b.accessibilityValue.longLongValue;
    NSDictionary *m=[self currentModule]; uint64_t base=H5XParseAddr(m[@"start"]);
    if(target>=base)self.offsetField.text=[NSString stringWithFormat:@"0x%llX",target-base];
    [self readARM64];
}
- (void)restoreAddress:(uint64_t)a {
    NSDictionary *found=nil; for(NSDictionary *p in self.patches)if(H5XParseAddr(p[@"address"])==a){found=p;break;}
    if(!found)return; uint32_t old=[found[@"original"] unsignedIntValue];
    if(![self writeWord:a word:old]){
        [self alert:[NSString stringWithFormat:@"復元に失敗しました\n\n%@",self.lastWriteStatus?:@"不明なエラー"]];
        return;
    }
    [self.patches removeObject:found];[self saveState];[self renderPatches];
    self.armStatus.text=[NSString stringWithFormat:@"RESTORE OK  %@",[self moduleOffset:a]];
    if(self.armStart)[self readARM64];
}
- (void)restoreAllPatches {
    NSArray *copy=[self.patches copy];
    NSMutableArray *failed=[NSMutableArray array];
    for(NSDictionary *p in copy){
        if(![self writeWord:H5XParseAddr(p[@"address"]) word:[p[@"original"] unsignedIntValue]]) [failed addObject:p];
    }
    self.patches=failed;[self saveState];[self renderPatches];
    if(failed.count)[self alert:[NSString stringWithFormat:@"%lu件の復元に失敗しました\n%@",(unsigned long)failed.count,self.lastWriteStatus?:@""]];
    if(self.armStart)[self readARM64];
}
- (void)renderPatches {
    if(!self.patchStack)return;[self clearStack:self.patchStack];
    for(NSInteger i=0;i<(NSInteger)self.patches.count;i++){
        NSDictionary *p=self.patches[i];uint64_t a=H5XParseAddr(p[@"address"]);
        UIView *c=[self card];UIStackView *s=[[UIStackView alloc] init];s.axis=UILayoutConstraintAxisVertical;s.spacing=4;s.translatesAutoresizingMaskIntoConstraints=NO;[c addSubview:s];
        [NSLayoutConstraint activateConstraints:@[[s.leadingAnchor constraintEqualToAnchor:c.leadingAnchor constant:9],[s.trailingAnchor constraintEqualToAnchor:c.trailingAnchor constant:-9],[s.topAnchor constraintEqualToAnchor:c.topAnchor constant:8],[s.bottomAnchor constraintEqualToAnchor:c.bottomAnchor constant:-8]]];
        [s addArrangedSubview:[self label:[self moduleOffset:a] size:10 color:H5XColor(142,171,217)]];
        [s addArrangedSubview:[self label:[NSString stringWithFormat:@"%@ → %@",H5XHex32([p[@"original"] unsignedIntValue]),H5XHex32([p[@"current"] unsignedIntValue])] size:10 color:H5XColor(221,231,246)]];
        UIButton *r=[self button:@"復元" action:@selector(restorePatchList:)];r.tag=i;[s addArrangedSubview:r];
        [self.patchStack addArrangedSubview:c];
    }
    if(self.patches.count==0)[self.patchStack addArrangedSubview:[self label:@"パッチなし" size:12 color:H5XColor(120,139,168)]];
}
- (void)restorePatchList:(UIButton*)b {
    if(b.tag<0||b.tag>=(NSInteger)self.patches.count)return;
    [self restoreAddress:H5XParseAddr(self.patches[b.tag][@"address"])];
}
- (void)readHex {
    if(!self.armStart){[self alert:@"先にARM64を読み込んでください"];return;}
    NSMutableString *out=[NSMutableString string];
    for(int off=0;off<128;off+=16){
        [out appendFormat:@"%@  ",H5XAddr(self.armStart+off)];
        NSMutableString *ascii=[NSMutableString string];
        for(int i=0;i<16;i++){uint8_t b=[self readU8:self.armStart+off+i];[out appendFormat:@"%02X ",b];[ascii appendFormat:@"%c",(b>=32&&b<127)?b:'.'];}
        [out appendFormat:@" %@\n",ascii];
    }
    self.hexView.text=out;
}

- (void)showDiag {
    NSString *s=[NSString stringWithFormat:@"Native UI: OK\nEngine: %@\nPID: %d / Target: %d\nPatchMode: iGG vm_protect(RW|COPY) -> vm_write -> RX\nResults: %ld\nModules: %lu\nPatches: %lu\nLastPatch: %@\nWindow: %.0fx%.0f",
                 h5gg?@"OK":@"NG",getpid(),h5gg.targetpid,[h5gg getResultsCount],(unsigned long)self.modules.count,(unsigned long)self.patches.count,
                 self.lastWriteStatus.length?self.lastWriteStatus:@"未実行",
                 H5XWindow.bounds.size.width,H5XWindow.bounds.size.height];
    [self alert:s];
}
- (void)alert:(NSString*)msg {
    UIAlertController *a=[UIAlertController alertControllerWithTitle:@"H5GGX" message:msg preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:a animated:YES completion:nil];
}
- (void)prompt:(NSString*)title value:(NSString*)value completion:(void(^)(NSString*))completion {
    UIAlertController *a=[UIAlertController alertControllerWithTitle:title message:nil preferredStyle:UIAlertControllerStyleAlert];
    [a addTextFieldWithConfigurationHandler:^(UITextField *f){f.text=value;f.autocapitalizationType=UITextAutocapitalizationTypeNone;}];
    [a addAction:[UIAlertAction actionWithTitle:@"キャンセル" style:UIAlertActionStyleCancel handler:nil]];
    [a addAction:[UIAlertAction actionWithTitle:@"適用" style:UIAlertActionStyleDefault handler:^(UIAlertAction *act){if(completion)completion(a.textFields.firstObject.text?:@"");}]];
    [self presentViewController:a animated:YES completion:nil];
}
- (void)closeWindow { H5XWindow.hidden=YES; }
- (void)dragWindow:(UIPanGestureRecognizer*)g {
    CGPoint t=[g translationInView:H5XWindow];
    if(g.state==UIGestureRecognizerStateChanged||g.state==UIGestureRecognizerStateEnded){
        CGRect f=H5XWindow.frame;f.origin.x+=t.x;f.origin.y+=t.y;
        CGRect b=UIScreen.mainScreen.bounds;
        f.origin.x=MAX(0,MIN(f.origin.x,b.size.width-f.size.width));
        f.origin.y=MAX(0,MIN(f.origin.y,b.size.height-f.size.height));
        H5XWindow.frame=f;[g setTranslation:CGPointZero inView:H5XWindow];
    }
}
@end

static CGRect H5XDefaultFrame(void) {
    CGRect b=UIScreen.mainScreen.bounds;
    CGFloat w=MIN(390.0,MAX(300.0,b.size.width-14.0));
    CGFloat h=MIN(650.0,MAX(360.0,b.size.height-60.0));
    return CGRectMake((b.size.width-w)/2.0,(b.size.height-h)/2.0,w,h);
}
static void H5GGXNativeShow(void) {
    if(!H5XWindow){
        if(@available(iOS 13.0,*)){
            UIWindowScene *scene=nil;
            for(UIWindowScene *s in UIApplication.sharedApplication.connectedScenes)if(s.activationState==UISceneActivationStateForegroundActive){scene=s;break;}
            H5XWindow=scene?[[UIWindow alloc] initWithWindowScene:scene]:[[UIWindow alloc] initWithFrame:H5XDefaultFrame()];
        }else H5XWindow=[[UIWindow alloc] initWithFrame:H5XDefaultFrame()];
        H5XWindow.frame=H5XDefaultFrame();
        H5XWindow.windowLevel=UIWindowLevelAlert-1;
        H5XWindow.backgroundColor=UIColor.clearColor;
        H5GGXNativeController *vc=[[H5GGXNativeController alloc] init];
        H5XController=vc;
        H5XWindow.rootViewController=vc;
    }
    H5XWindow.hidden=NO;
}
static void H5GGXNativeToggle(void) {
    if(!H5XWindow||H5XWindow.hidden)H5GGXNativeShow();
    else H5XWindow.hidden=YES;
}
void H5GGXNativeInit(void) {
    if(!h5gg)h5gg=[[h5ggEngine alloc] init];
    if(!floatBtn)initFloatButton(^{ H5GGXNativeToggle(); });
    H5GGXNativeShow();
}
