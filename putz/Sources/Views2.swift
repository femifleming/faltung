// disk, duplicates, leftovers, tune, settings
import SwiftUI
import AppKit

// MARK: - dk4 disk

struct DiskView: View {
    @EnvironmentObject var d: DiskScan
    @Environment(\.theme) var t
    @State var tab = 0
    @State var confirm: Node? = nil

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 12) {
                Text(Fmt.tilde(d.rootPath)).font(.mono(12, medium: true))
                FLink(text: "choose") { if let p = pickFolder(d.rootPath) { d.rootPath = p } }
                FLink(text: "whole disk") { d.rootPath = "/" }
                FLink(text: d.busy ? "scanning…" : "scan", primary: true, disabled: d.busy) { Task { await d.scan() } }
                if d.busy { FLink(text: "stop") { d.cancel() } }
                Spacer()
                Tabs(items: [(0, "folders"), (1, "largest"), (2, "old & large"), (3, "types")], selection: $tab)
            }
            if d.busy {
                Text("\(Fmt.count(d.scannedFiles)) files so far").font(.mono(11))
                Spacer()
            } else if d.root != nil {
                switch tab {
                case 0: folders
                case 1: fileList(d.largest, note: "files over 50 mb")
                case 2: fileList(d.oldBig, note: "over 50 mb, not opened for a year")
                default: types
                }
            } else {
                Text("scans with sizes on disk; files under 1 mb are summed per folder. scanning / needs full disk access to see everything.")
                    .font(.sans(12)).foregroundStyle(t.dim)
                Spacer()
            }
            Footer(status: d.status)
        }
        .alert("move \(confirm?.name ?? "") to the bin?", isPresented: Binding(get: { confirm != nil }, set: { if !$0 { confirm = nil } })) {
            Button("move to bin", role: .destructive) { if let n = confirm { d.trash(n) } }
            Button("cancel", role: .cancel) {}
        } message: { Text("\(Fmt.bytes(confirm?.bytes ?? 0)) · \(Fmt.tilde(confirm?.path ?? ""))") }
    }

    var crumbs: [Node] {
        var l: [Node] = [], n = d.current
        while let x = n { l.insert(x, at: 0); n = x.parent }
        return l
    }

    @ViewBuilder var folders: some View {
        if let cur = d.current {
            HStack(spacing: 4) {
                ForEach(crumbs) { n in
                    Text(n.name.isEmpty ? "/" : n.name).font(.mono(11, medium: n === cur)).onTapGesture { d.current = n }
                    if n !== cur { Text("/").font(.mono(11)).foregroundStyle(t.dim) }
                }
                Spacer()
                Text("\(Fmt.bytes(cur.bytes)) · \(Fmt.count(cur.files)) files").font(.mono(11))
            }
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 2) {
                    ForEach(cur.children.prefix(400)) { n in
                        HStack(spacing: 10) {
                            Text(n.isDir ? "▸" : " ").font(.mono(10)).frame(width: 10)
                            Text(n.name).font(.mono(11)).lineLimit(1).truncationMode(.middle).frame(width: 280, alignment: .leading)
                            SkewBar(value: Double(n.bytes) / Double(max(1, cur.bytes)), fill: n.isDir ? nil : Color(hex: Kind.of((n.name as NSString).pathExtension).hex))
                            Text(Fmt.bytes(n.bytes)).font(.mono(11)).frame(width: 74, alignment: .trailing)
                            Text(String(format: "%4.1f%%", 100 * Double(n.bytes) / Double(max(1, cur.bytes)))).font(.mono(10)).foregroundStyle(t.dim).frame(width: 48, alignment: .trailing)
                            if !n.path.isEmpty {
                                Text("↗").font(.mono(11)).onTapGesture { Files.reveal(n.path) }
                                Text("×").font(.mono(12)).onTapGesture { confirm = n }
                            } else { Spacer().frame(width: 24) }
                        }
                        .padding(.vertical, 2)
                        .contentShape(Rectangle())
                        .onTapGesture { if n.isDir && !n.children.isEmpty { d.current = n } }
                    }
                }
            }
        }
    }

    func fileList(_ list: [Node], note: String) -> some View {
        let maxB = list.first?.bytes ?? 1
        return VStack(alignment: .leading, spacing: 6) {
            Text("\(list.count) \(note) · \(Fmt.bytes(list.reduce(0) { $0 + $1.bytes }))").font(.mono(11)).foregroundStyle(t.dim)
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 2) {
                    ForEach(list) { n in
                        HStack(spacing: 10) {
                            Chip(color: Color(hex: Kind.of((n.name as NSString).pathExtension).hex))
                            VStack(alignment: .leading, spacing: 0) {
                                Text(n.name).font(.mono(11)).lineLimit(1)
                                Text(Fmt.tilde((n.path as NSString).deletingLastPathComponent)).font(.mono(9)).foregroundStyle(t.dim).lineLimit(1).truncationMode(.middle)
                            }.frame(width: 360, alignment: .leading)
                            SkewBar(value: Double(n.bytes) / Double(max(1, maxB)))
                            Text(Fmt.ago(n.accessed ?? n.modified)).font(.mono(10)).foregroundStyle(t.dim).frame(width: 70, alignment: .trailing)
                            Text(Fmt.bytes(n.bytes)).font(.mono(11)).frame(width: 70, alignment: .trailing)
                            Text("↗").font(.mono(11)).onTapGesture { Files.reveal(n.path) }
                            Text("×").font(.mono(12)).onTapGesture { confirm = n }
                        }
                        .padding(.vertical, 2)
                    }
                }
            }
        }
    }

    var types: some View {
        let total = max(1, d.kinds.reduce(0) { $0 + $1.1 })
        return VStack(alignment: .leading, spacing: 12) {
            // one strip, the whole scan
            GeometryReader { g in
                HStack(spacing: 2) {
                    ForEach(d.kinds, id: \.0) { k, b in
                        Skew(skew: 0.4).fill(Color(hex: k.hex)).frame(width: max(2, g.size.width * CGFloat(b) / CGFloat(total) - 2))
                    }
                }
            }.frame(height: 26)
            ForEach(d.kinds, id: \.0) { k, b in
                HStack(spacing: 12) {
                    Chip(color: Color(hex: k.hex))
                    Text(k.rawValue).font(.mono(11)).frame(width: 90, alignment: .leading)
                    SkewBar(value: Double(b) / Double(d.kinds.first?.1 ?? 1), fill: Color(hex: k.hex))
                    Text(Fmt.bytes(b)).font(.mono(11)).frame(width: 74, alignment: .trailing)
                    Text(String(format: "%.1f%%", 100 * Double(b) / Double(total))).font(.mono(10)).foregroundStyle(t.dim).frame(width: 50, alignment: .trailing)
                }
            }
            Spacer()
        }
    }
}

// MARK: - dp5 duplicates

struct DupesView: View {
    @EnvironmentObject var d: Duplicates
    @Environment(\.theme) var t
    @State var minIdx = 1
    @State var confirm = false
    let mins: [Double] = [0.1, 1, 10, 100]

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .top, spacing: 24) {
                VStack(alignment: .leading, spacing: 3) {
                    Text("look in").font(.mono(10)).foregroundStyle(t.dim)
                    ForEach(d.folders, id: \.self) { f in
                        HStack { Text(Fmt.tilde(f)).font(.mono(11)); Text("×").font(.mono(11)).onTapGesture { d.folders.removeAll { $0 == f } } }
                    }
                    Text("+ add folder").font(.mono(11)).onTapGesture { if let p = pickFolder(), !d.folders.contains(p) { d.folders.append(p) } }
                }
                VStack(alignment: .leading, spacing: 6) {
                    Text("smallest file").font(.mono(10)).foregroundStyle(t.dim)
                    Tabs(items: [(0, "100 kb"), (1, "1 mb"), (2, "10 mb"), (3, "100 mb")], selection: $minIdx)
                        .onChange(of: minIdx) { _, v in d.minMB = mins[v] }
                }
                Spacer()
                VStack(alignment: .trailing, spacing: 6) {
                    HStack(spacing: 10) {
                        FLink(text: d.busy ? "searching…" : "search", primary: true, disabled: d.busy) { Task { await d.scan() } }
                        if d.busy { FLink(text: "stop") { d.cancel() } }
                    }
                    Text(d.stage).font(.mono(10)).foregroundStyle(t.dim)
                }
            }
            if !d.groups.isEmpty {
                HStack(spacing: 10) {
                    Text("tick all but").font(.mono(10)).foregroundStyle(t.dim)
                    FLink(text: "the oldest") { d.autoPick(keepOldest: true) }
                    FLink(text: "the newest") { d.autoPick(keepOldest: false) }
                    FLink(text: "the one in…") { if let f = pickFolder() { d.autoPickKeep(inFolder: f) } }
                    Spacer()
                    FLink(text: "move \(Fmt.bytes(d.pickedBytes)) to bin", primary: true, disabled: d.busy || d.pickedBytes == 0) { confirm = true }
                }
            }
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 10) {
                    ForEach($d.groups) { $g in
                        VStack(alignment: .leading, spacing: 2) {
                            Hairline(label: "\(g.files.count) × \(Fmt.bytes(g.bytes))", trailing: "\(Fmt.bytes(g.waste)) extra")
                            ForEach($g.files) { $f in
                                HStack(spacing: 8) {
                                    SkewCheck(on: $f.picked)
                                    Text(Fmt.tilde(f.path)).font(.mono(10)).lineLimit(1).truncationMode(.middle)
                                    Spacer()
                                    Text(Fmt.date(f.modified)).font(.mono(10)).foregroundStyle(t.dim)
                                    Text("↗").font(.mono(10)).onTapGesture { Files.reveal(f.path) }
                                }
                            }
                        }
                    }
                }
            }
            Footer(status: d.status)
        }
        .alert("move \(d.groups.flatMap(\.files).filter(\.picked).count) copies to the bin?", isPresented: $confirm) {
            Button("move", role: .destructive) { Task { await d.trashPicked() } }
            Button("cancel", role: .cancel) {}
        } message: { Text("one copy of every file is always kept.") }
    }
}

// MARK: - or6 leftovers

struct LeftoversView: View {
    @EnvironmentObject var o: Orphans
    @Environment(\.theme) var t
    @State var confirm = false

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 12) {
                FLink(text: o.busy ? "searching…" : "search", primary: true, disabled: o.busy) { Task { await o.scan() } }
                if o.scanned {
                    FLink(text: "tick all") { for i in o.items.indices { o.items[i].picked = true } }
                    FLink(text: "tick none") { for i in o.items.indices { o.items[i].picked = false } }
                    Spacer()
                    FLink(text: "move ticked to bin", primary: true, disabled: o.busy || !(o.items + o.broken).contains(where: \.picked)) { confirm = true }
                }
            }
            Text("only folders named like an app id (com.vendor.app) whose app and vendor are both gone are listed. nothing is ticked for you: look before you move.")
                .font(.sans(11)).foregroundStyle(t.dim)
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 2) {
                    if !o.items.isEmpty { Hairline(label: "removed apps", trailing: "\(o.items.count)").padding(.bottom, 4) }
                    ForEach($o.items) { $it in row($it) }
                    if !o.broken.isEmpty { Hairline(label: "broken preference files", trailing: "\(o.broken.count)").padding(.vertical, 6) }
                    ForEach($o.broken) { $it in row($it) }
                }
            }
            Footer(status: o.status)
        }
        .alert("move \((o.items + o.broken).filter(\.picked).count) items to the bin?", isPresented: $confirm) {
            Button("move", role: .destructive) { Task { await o.removePicked() } }
            Button("cancel", role: .cancel) {}
        }
    }

    func row(_ it: Binding<Orphan>) -> some View {
        let v = it.wrappedValue
        return HStack(spacing: 8) {
            SkewCheck(on: it.picked)
            Text(v.bundleID).font(.mono(11)).lineLimit(1).frame(width: 300, alignment: .leading)
            Text(v.place).font(.mono(10)).foregroundStyle(t.dim).frame(width: 110, alignment: .leading)
            Text(Fmt.date(v.modified)).font(.mono(10)).foregroundStyle(t.dim)
            Spacer()
            Text(v.bytes.map(Fmt.bytes) ?? "?").font(.mono(11)).frame(width: 70, alignment: .trailing)
            Text("↗").font(.mono(10)).onTapGesture { Files.reveal(v.path) }
        }
        .padding(.vertical, 1)
    }
}

// MARK: - mt7 tune

struct TuneView: View {
    @EnvironmentObject var m: Maintenance
    @Environment(\.theme) var t

    var body: some View {
        HStack(alignment: .top, spacing: 28) {
            ScrollView {
                VStack(alignment: .leading, spacing: 6) {
                    ForEach(m.tasks) { task in
                        HStack(spacing: 12) {
                            VStack(alignment: .leading, spacing: 1) {
                                Text(task.name).font(.mono(12, medium: true))
                                Text(task.note).font(.sans(11)).foregroundStyle(t.dim)
                            }
                            Spacer()
                            if task.admin { Text("password").font(.mono(9)).foregroundStyle(t.dim) }
                            FLink(text: m.running == task.id ? "running…" : "run", disabled: m.running != nil) { Task { await m.run(task) } }
                        }
                        .padding(.vertical, 4)
                        Rectangle().fill(t.ink.opacity(0.15)).frame(height: 1)
                    }
                    if !m.snapshots.isEmpty {
                        Hairline(label: "local snapshots", trailing: "\(m.snapshots.count)").padding(.top, 10)
                        ForEach(m.snapshots, id: \.self) { Text($0).font(.mono(10)) }
                    }
                }
            }
            .frame(maxWidth: 560)
            VStack(alignment: .leading, spacing: 6) {
                Hairline(label: "output")
                ScrollView {
                    VStack(alignment: .leading, spacing: 8) {
                        ForEach(Array(m.log.enumerated()), id: \.offset) { _, e in
                            VStack(alignment: .leading, spacing: 2) {
                                Text("\(Fmt.date(e.0)) · \(e.1)").font(.mono(10, medium: true))
                                Text(e.2).font(.mono(10)).textSelection(.enabled)
                            }
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
            .padding(12).background(t.paper.opacity(0.7))
        }
        .task { await m.loadSnapshots() }
    }
}

// MARK: - se8 settings

struct SettingsView: View {
    @ObservedObject var p = Prefs.shared
    @EnvironmentObject var cleaner: Cleaner
    @Environment(\.theme) var t
    @State var fda = Access.fullDisk

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                Hairline(label: "removal")
                HStack(spacing: 10) {
                    Text("cleaned files").font(.mono(11)).frame(width: 190, alignment: .leading)
                    Tabs(items: [(false, "erase"), (true, "move to bin")], selection: $p.useTrash)
                }
                Text("caches are safe to erase. uninstalls, duplicates, leftovers and disk always go to the bin.")
                    .font(.sans(11)).foregroundStyle(t.dim)
                Stepper1(label: "temp files older than", value: $p.tempAgeDays, range: 1...60, unit: "days")
                Stepper1(label: "logs older than", value: $p.logAgeDays, range: 0...365, unit: "days", zero: "all")
                Stepper1(label: "installers older than", value: $p.downloadsAgeDays, range: 0...365, unit: "days", zero: "all")
                HStack(spacing: 10) {
                    Text("skip apps that are open").font(.mono(11)).frame(width: 190, alignment: .leading)
                    SkewCheck(on: $p.skipRunning)
                }

                Hairline(label: "look").padding(.top, 8)
                HStack(spacing: 10) {
                    Text("appearance").font(.mono(11)).frame(width: 190, alignment: .leading)
                    Tabs(items: [(0, "follow mac"), (1, "light · faltung"), (2, "dark · deep")], selection: $p.appearance)
                }

                Hairline(label: "access").padding(.top, 8)
                HStack(spacing: 10) {
                    Text("full disk access").font(.mono(11)).frame(width: 190, alignment: .leading)
                    Text(fda ? "on" : "off").font(.mono(11, medium: true))
                    FLink(text: "open settings") { Access.openSettings() }
                    FLink(text: "check") { fda = Access.fullDisk }
                }

                Hairline(label: "never touch", trailing: "\(p.exclusions.count)").padding(.top, 8)
                pathList($p.exclusions)
                Hairline(label: "include (cleaned under custom → your folders)", trailing: "\(p.includes.count)").padding(.top, 8)
                pathList($p.includes)

                Hairline(label: "rules").padding(.top, 8)
                HStack(spacing: 10) {
                    FLink(text: "reset ticks to defaults") { p.ruleState = [:]; cleaner.reloadRules() }
                }

                Hairline(label: "history", trailing: Fmt.bytes(History.shared.total)).padding(.top, 8)
                ForEach(History.shared.records.prefix(40)) { r in
                    HStack {
                        Text(Fmt.date(r.date)).font(.mono(10)).foregroundStyle(t.dim).frame(width: 84, alignment: .leading)
                        Text(r.what).font(.mono(10))
                        Spacer()
                        Text(Fmt.bytes(r.bytes)).font(.mono(10))
                    }
                }
                Text("putz · local build · \(Stats.os)").font(.mono(9)).foregroundStyle(t.dim).padding(.top, 12)
            }
            .frame(maxWidth: 700, alignment: .leading)
        }
    }

    func pathList(_ list: Binding<[String]>) -> some View {
        VStack(alignment: .leading, spacing: 3) {
            ForEach(list.wrappedValue, id: \.self) { f in
                HStack {
                    Text(Fmt.tilde(f)).font(.mono(11))
                    Text("×").font(.mono(11)).onTapGesture { list.wrappedValue.removeAll { $0 == f } }
                }
            }
            Text("+ add folder").font(.mono(11)).onTapGesture {
                if let f = pickFolder(), !list.wrappedValue.contains(f) {
                    if Safety.protectedRoots.contains(f.standardizedPath) && list.wrappedValue != p.exclusions { return }
                    list.wrappedValue.append(f)
                }
            }
        }
    }
}
