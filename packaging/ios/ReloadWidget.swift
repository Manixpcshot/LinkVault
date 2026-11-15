import WidgetKit

@_cdecl("reloadLinkVaultNotesWidget")
public func reloadLinkVaultNotesWidget() {
    WidgetCenter.shared.reloadTimelines(ofKind: "LinkVaultNotes")
}
