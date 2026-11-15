import Foundation
import SwiftUI
import WidgetKit

private let group = "group.io.github.manixpcshot.linkvault"

struct NoteEntry: TimelineEntry {
    let date: Date
    let title: String
    let preview: String
    let deadline: Date?
}

struct NotesTimeline: TimelineProvider {
    func placeholder(in context: Context) -> NoteEntry {
        let now = Date()
        return NoteEntry(date: now, title: "Going out", preview: "Leave for the station",
                         deadline: now.addingTimeInterval(5400))
    }

    func getSnapshot(in context: Context, completion: @escaping (NoteEntry) -> Void) {
        completion(context.isPreview ? placeholder(in: context) : current())
    }

    func getTimeline(in context: Context, completion: @escaping (Timeline<NoteEntry>) -> Void) {
        let entry = current()
        if let deadline = entry.deadline, deadline > entry.date {
            let finished = NoteEntry(date: deadline, title: entry.title, preview: entry.preview, deadline: deadline)
            completion(Timeline(entries: [entry, finished], policy: .never))
        } else {
            completion(Timeline(entries: [entry], policy: .never))
        }
    }

    private func current() -> NoteEntry {
        let now = Date()
        guard let container = FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: group),
              let data = try? Data(contentsOf: container.appendingPathComponent("note-widget.json")),
              let saved = try? JSONDecoder().decode(Snapshot.self, from: data),
              !saved.title.isEmpty else {
            return NoteEntry(date: now, title: "Add a note in LinkVault", preview: "", deadline: nil)
        }
        let deadline = saved.deadline > 0 ? Date(timeIntervalSince1970: saved.deadline) : nil
        return NoteEntry(date: now, title: saved.title, preview: saved.preview, deadline: deadline)
    }

    private struct Snapshot: Decodable {
        let title: String
        let preview: String
        let deadline: Double
    }
}

struct NotesWidgetView: View {
    let entry: NoteEntry
    private let surface = Color(red: 0.067, green: 0.063, blue: 0.094)
    private let accent = Color(red: 0.76, green: 0.69, blue: 1)

    @ViewBuilder var body: some View {
        if #available(iOSApplicationExtension 17.0, *) {
            content.containerBackground(surface, for: .widget)
        } else {
            content.padding(13).background(surface)
        }
    }

    private var content: some View {
        VStack(alignment: .leading, spacing: 5) {
            Label("LinkVault Notes", systemImage: "note.text")
                .font(.caption).fontWeight(.semibold).foregroundStyle(accent)
            Text(verbatim: entry.title).font(.headline).foregroundStyle(.white).lineLimit(2)
            if let end = entry.deadline {
                if end > entry.date {
                    Text(timerInterval: entry.date...end, pauseTime: end, countsDown: true)
                        .font(.title3.bold()).monospacedDigit().foregroundStyle(accent)
                } else {
                    Text("Time’s up").font(.subheadline.bold()).foregroundStyle(accent)
                }
            }
            if !entry.preview.isEmpty {
                Text(verbatim: entry.preview).font(.caption).foregroundStyle(.white.opacity(0.75)).lineLimit(2)
            }
            Spacer(minLength: 0)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }
}

@main
struct LinkVaultNotesWidget: Widget {
    var body: some WidgetConfiguration {
        StaticConfiguration(kind: "LinkVaultNotes", provider: NotesTimeline()) { entry in
            NotesWidgetView(entry: entry)
        }
        .configurationDisplayName("LinkVault Notes")
        .description("The selected note and its countdown")
        .supportedFamilies([.systemSmall, .systemMedium])
    }
}
