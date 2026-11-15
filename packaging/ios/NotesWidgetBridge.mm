#import <Foundation/Foundation.h>
#include "notebook.h"

extern "C" void reloadLinkVaultNotesWidget();

void updateIosNoteWidget(const Note& note) {
    @autoreleasepool {
        NSURL* container = [[NSFileManager defaultManager]
            containerURLForSecurityApplicationGroupIdentifier:@"group.io.github.manixpcshot.linkvault"];
        if (!container) return;
        NSURL* path = [container URLByAppendingPathComponent:@"note-widget.json"];
        NSDictionary* snapshot = @{
            @"title": note.id.isEmpty() ? @"" : note.displayTitle().left(100).toNSString(),
            @"preview": note.body.simplified().left(240).toNSString(),
            @"deadline": @(note.deadline)
        };
        NSData* data = [NSJSONSerialization dataWithJSONObject:snapshot options:NSJSONWritingSortedKeys error:nil];
        if (data && ![data isEqualToData:[NSData dataWithContentsOfURL:path]]
            && [data writeToURL:path options:NSDataWritingAtomic error:nil]) {
            reloadLinkVaultNotesWidget();
        }
    }
}
