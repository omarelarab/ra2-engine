// iOS OS hooks (platform_os.hpp, RA2_IOS block). Compiled only by the iOS
// build (Package.swift, ObjC++ with ARC); the desktop and Android builds never
// see this file.
//
// - os_ios_documents_dir(): the app's Documents folder, which Info.plist
//   (UIFileSharingEnabled + LSSupportsOpeningDocumentsInPlace) shows in the
//   Files app ("On My iPhone/iPad > RA2 Engine") and in Finder file sharing.
//   The engine reads the game files from Documents/ra2/ (data_dir.inc).
// - os_ios_bundle_data_dir(): <app>/GameData, present only in a personal test
//   build that bundled the user's own files (ios/scripts/build.sh --game-data).
// - os_ios_import_game_files(): the in-app importer, the iOS counterpart of
//   Android's SetupActivity: a Files folder picker, then the files the engine
//   reads are copied into Documents/ra2/ under their canonical names (same
//   rules as SetupActivity.target()).
//
// SDL2 runs SDL_main on the UIKit main thread, so the importer presents its
// view controllers directly and keeps the run loop turning until the player
// is done; the copy itself runs on a background queue.

#if !__has_feature(objc_arc)
#error "platform_ios.mm needs ARC (-fobjc-arc)"
#endif

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <SDL.h>

#include <string>

#include "platform_os.hpp"

@interface RA2FolderPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic) BOOL done;
@property(nonatomic, strong) NSURL* picked;
@end

@implementation RA2FolderPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    self.picked = urls.firstObject;
    self.done = YES;
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
    self.done = YES;
}
@end

namespace {

std::string utf8(NSString* s) {
    const char* c = s.UTF8String;
    return c ? std::string(c) : std::string();
}

NSString* documents_path() {
    NSArray<NSString*>* a = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    return a.firstObject ?: [NSHomeDirectory() stringByAppendingPathComponent:@"Documents"];
}

// Keep the run loop (UIKit events, the main dispatch queue) turning until
// `done` says so. SDL keeps queueing its own events meanwhile; they are
// flushed afterwards so taps on the picker do not reach the game.
void spin_until(BOOL (^done)(void)) {
    while (!done()) {
        @autoreleasepool {
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                     beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        }
    }
}

void flush_sdl_input() {
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_KEYDOWN, SDL_MULTIGESTURE);
}

UIViewController* top_view_controller() {
    UIWindow* key = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) continue;
        for (UIWindow* w in ((UIWindowScene*)scene).windows) {
            if (w.isKeyWindow) { key = w; break; }
            if (!key) key = w;
        }
        if (key.isKeyWindow) break;
    }
    UIViewController* vc = key.rootViewController;
    while (vc.presentedViewController) vc = vc.presentedViewController;
    return vc;
}

// A modal alert with one OK button; returns when it is dismissed.
void show_message(NSString* title, NSString* message) {
    UIViewController* host = top_view_controller();
    if (!host) {
        SDL_Log("[import] %s: %s", title.UTF8String, message.UTF8String);
        return;
    }
    __block BOOL closed = NO;
    UIAlertController* a = [UIAlertController alertControllerWithTitle:title
                                                                message:message
                                                         preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"OK"
                                          style:UIAlertActionStyleDefault
                                        handler:^(UIAlertAction* action) { (void)action; closed = YES; }]];
    [host presentViewController:a animated:YES completion:nil];
    spin_until(^BOOL { return closed; });
}

// The files the engine reads; the first four are required (data_dir.inc
// kDataFiles). Same list and spelling as Android's SetupActivity.FILES.
NSArray<NSString*>* fixed_files() {
    return @[ @"ra2.mix", @"ra2md.mix", @"language.mix", @"multimd.mix",
              @"langmd.mix", @"mapsmd03.mix", @"theme.mix", @"expand01.mix",
              @"Maps01.mix", @"MAPS02.MIX", @"ra2.csf" ];
}
constexpr NSUInteger kRequired = 4;

BOOL matches(NSString* s, NSString* pattern) {
    return [[NSPredicate predicateWithFormat:@"SELF MATCHES %@", pattern] evaluateWithObject:s];
}

// SetupActivity.target(): where a picked file goes (relative to Documents/ra2),
// or nil when the engine does not read it.
NSString* target_for(NSString* name) {
    NSString* l = name.lowercaseString;
    for (NSString* f in fixed_files())
        if ([f.lowercaseString isEqualToString:l]) return f;
    if (matches(l, @"(expand|ecache|elocal)[0-9][0-9]\\.mix")) return l;
    if ([l isEqualToString:@"thememd.mix"]) return l;
    if (matches(l, @"(movies[0-9][0-9]|movmd[0-9][0-9])\\.mix")) return l;
    if ([l hasSuffix:@".bik"] && l.length > 4) return [@"Movies" stringByAppendingPathComponent:l];
    return nil;
}

NSArray<NSURL*>* children(NSURL* dir) {
    NSArray<NSURL*>* c = [NSFileManager.defaultManager
          contentsOfDirectoryAtURL:dir
        includingPropertiesForKeys:@[ NSURLIsDirectoryKey, NSURLFileSizeKey ]
                           options:NSDirectoryEnumerationSkipsHiddenFiles
                             error:nil];
    return c ?: @[];
}

BOOL is_dir(NSURL* u) {
    NSNumber* d = nil;
    [u getResourceValue:&d forKey:NSURLIsDirectoryKey error:nil];
    return d.boolValue;
}

long long file_size(NSURL* u) {
    NSNumber* n = nil;
    [u getResourceValue:&n forKey:NSURLFileSizeKey error:nil];
    return n ? n.longLongValue : -1;
}

// Collect target -> source URL from one folder; sub-folders are returned.
void scan(NSURL* dir, NSMutableDictionary<NSString*, NSURL*>* found, NSMutableArray<NSURL*>* subdirs) {
    for (NSURL* u in children(dir)) {
        if (is_dir(u)) {
            if (subdirs) [subdirs addObject:u];
            continue;
        }
        NSString* t = target_for(u.lastPathComponent);
        if (t && !found[t]) found[t] = u;
    }
}

// SetupActivity.importFrom's search: the picked folder, then (when it holds
// none of the required files) one level of sub-folders -- picking the folder
// that CONTAINS RA2 works too; a Movies folder is always read.
NSDictionary<NSString*, NSURL*>* collect(NSURL* root) {
    NSMutableDictionary<NSString*, NSURL*>* found = [NSMutableDictionary dictionary];
    NSMutableArray<NSURL*>* subdirs = [NSMutableArray array];
    scan(root, found, subdirs);
    BOOL any_required = NO;
    for (NSUInteger i = 0; i < kRequired; ++i) any_required |= found[fixed_files()[i]] != nil;
    NSMutableArray<NSURL*>* deeper = [NSMutableArray array];
    for (NSURL* sub in subdirs) {
        const BOOL movies = [sub.lastPathComponent caseInsensitiveCompare:@"movies"] == NSOrderedSame;
        if (!any_required || movies) scan(sub, found, any_required ? nil : deeper);
    }
    for (NSURL* sub in deeper)
        if ([sub.lastPathComponent caseInsensitiveCompare:@"movies"] == NSOrderedSame) scan(sub, found, nil);
    return found;
}

// Copy one file through NSFileCoordinator (downloads an iCloud Drive file
// that is not on the device yet) to <dst>.part, then rename over <dst>.
BOOL copy_one(NSURL* src, NSString* dst, NSString** err) {
    NSFileManager* fm = NSFileManager.defaultManager;
    NSString* part = [dst stringByAppendingString:@".part"];
    [fm createDirectoryAtPath:dst.stringByDeletingLastPathComponent
  withIntermediateDirectories:YES attributes:nil error:nil];
    [fm removeItemAtPath:part error:nil];
    __block BOOL ok = NO;
    __block NSError* copy_error = nil;
    NSError* coord_error = nil;
    NSFileCoordinator* fc = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
    [fc coordinateReadingItemAtURL:src options:NSFileCoordinatorReadingWithoutChanges error:&coord_error
                        byAccessor:^(NSURL* readable) {
        ok = [fm copyItemAtURL:readable toURL:[NSURL fileURLWithPath:part] error:&copy_error];
    }];
    if (ok) {
        [fm removeItemAtPath:dst error:nil];
        NSError* mv = nil;
        ok = [fm moveItemAtPath:part toPath:dst error:&mv];
        if (!ok) copy_error = mv;
    }
    if (!ok) {
        [fm removeItemAtPath:part error:nil];
        NSError* e = coord_error ?: copy_error;
        if (err) *err = [NSString stringWithFormat:@"%@: %@", src.lastPathComponent,
                         e.localizedDescription ?: @"copy failed"];
    }
    return ok;
}

}  // namespace

namespace ra2engine {

std::string os_ios_documents_dir() {
    return utf8(documents_path());
}

std::string os_ios_bundle_data_dir() {
    NSString* d = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"GameData"];
    BOOL dir = NO;
    if (d && [NSFileManager.defaultManager fileExistsAtPath:d isDirectory:&dir] && dir) return utf8(d);
    return {};
}

int os_ios_import_game_files(bool restart_note) {
    UIViewController* host = top_view_controller();
    if (!host) {
        SDL_Log("[import] no view controller to present the folder picker");
        return -1;
    }

    // 1. Pick a folder (Files app: iCloud Drive, On My iPhone/iPad, USB drive, SMB).
    RA2FolderPickerDelegate* del = [RA2FolderPickerDelegate new];
    UIDocumentPickerViewController* picker =
        [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeFolder ]];
    picker.delegate = del;
    picker.allowsMultipleSelection = NO;
    picker.modalPresentationStyle = UIModalPresentationFormSheet;
    [host presentViewController:picker animated:YES completion:nil];
    spin_until(^BOOL { return del.done; });
    NSURL* root = del.picked;
    if (!root) {
        flush_sdl_input();
        return 0;
    }

    const BOOL scoped = [root startAccessingSecurityScopedResource];
    NSDictionary<NSString*, NSURL*>* found = collect(root);
    if (found.count == 0) {
        if (scoped) [root stopAccessingSecurityScopedResource];
        show_message(@"No game files found",
                     @"That folder holds none of the Red Alert 2 files the engine reads "
                     @"(ra2.mix, ra2md.mix, language.mix, multimd.mix, ...). Pick the folder "
                     @"you copied from your Red Alert 2 install.");
        flush_sdl_input();
        return 0;
    }

    // 2. Copy on a background queue; a progress alert follows along.
    NSString* data_dir = [documents_path() stringByAppendingPathComponent:@"ra2"];
    NSArray<NSString*>* names = [found.allKeys sortedArrayUsingSelector:@selector(compare:)];
    UIAlertController* progress = [UIAlertController alertControllerWithTitle:@"Importing game files"
                                                                       message:@"Starting..."
                                                                preferredStyle:UIAlertControllerStyleAlert];
    __block BOOL shown = NO;
    [top_view_controller() presentViewController:progress animated:YES completion:^{ shown = YES; }];
    spin_until(^BOOL { return shown; });

    // Written on the copy queue, read here only after `finished` (which is
    // set on the main queue after the loop, so the writes happen-before).
    __block BOOL finished = NO;
    __block int copied = 0;
    __block NSMutableArray<NSString*>* empty = [NSMutableArray array];
    __block NSString* error = nil;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSUInteger i = 0;
        for (NSString* t in names) {
            ++i;
            NSURL* src = found[t];
            NSString* msg = [NSString stringWithFormat:@"%@\n(%lu of %lu)", t,
                             (unsigned long)i, (unsigned long)names.count];
            dispatch_async(dispatch_get_main_queue(), ^{ progress.message = msg; });
            // Copied even when empty (as Android's SetupActivity does), but
            // named in the report: an empty archive passes the engine's
            // presence check and holds nothing (a stripped install).
            if (file_size(src) == 0) [empty addObject:t];
            NSString* err = nil;
            if (!copy_one(src, [data_dir stringByAppendingPathComponent:t], &err)) {
                error = err;
                break;
            }
            ++copied;
            SDL_Log("[import] %s", t.UTF8String);
        }
        dispatch_async(dispatch_get_main_queue(), ^{ finished = YES; });
    });
    spin_until(^BOOL { return finished; });
    if (scoped) [root stopAccessingSecurityScopedResource];

    __block BOOL dismissed = NO;
    [progress dismissViewControllerAnimated:YES completion:^{ dismissed = YES; }];
    spin_until(^BOOL { return dismissed; });

    // 3. Report.
    NSMutableString* text = [NSMutableString string];
    if (error) [text appendFormat:@"The copy stopped: %@\n\n", error];
    [text appendFormat:@"%d file(s) copied into Documents/ra2.", copied];
    if (empty.count)
        [text appendFormat:@"\n\nWarning, empty file(s) in that folder: %@.", [empty componentsJoinedByString:@", "]];
    if (copied > 0 && restart_note && !error)
        [text appendString:@"\n\nThe app closes now; open it again to load the new files."];
    show_message(error ? @"Import incomplete" : @"Import done", text);
    flush_sdl_input();
    SDL_Log("[import] %d copied, %lu empty%s%s", copied, (unsigned long)empty.count,
            error ? ", error: " : "", error ? error.UTF8String : "");
    return error ? -1 : copied;
}

}  // namespace ra2engine
