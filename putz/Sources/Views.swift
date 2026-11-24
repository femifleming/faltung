// the pages
import SwiftUI
import AppKit
import UniformTypeIdentifiers

// MARK: - shared bits

struct Panel<C: View>: View {
    @ViewBuilder var content: C
    @Environment(\.theme) var t
    var body: some View {
        content.padding(14).frame(maxWidth: .infinity, alignment: .topLeading)
            .background(t.paper.opacity(t.dark ? 0.9 : 0.86))
    }
}

struct Footer: View {
    var status: String
    var progress: Double? = nil
    @Environment(\.theme) var t
    var body: some View {
        HStack(spacing: 12) {
            Text(status).font(.mono(11)).lineLimit(1).truncationMode(.middle)
            if let p = progress { SkewBar(value: p).frame(width: 160) }
            Spacer()
        }
    }
}

struct Stat: View {
    var label: String
    var value: String
    var sub: String = ""
    @Environment(\.theme) var t
    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label).font(.mono(10)).foregroundStyle(t.dim)
            Text(value).font(.sans(20, medium: true))
            if !sub.isEmpty { Text(sub).font(.mono(10)).foregroundStyle(t.dim) }
        }
    }
}

struct Field: View {
    var placeholder: String
    @Binding var text: String
    @Environment(\.theme) var t
    var body: some View {
        VStack(spacing: 2) {
            TextField(placeholder, text: $text).textFieldStyle(.plain).font(.mono(11))
            Rectangle().fill(t.ink.opacity(0.35)).frame(height: 1)
        }
    }
}

struct Stepper1: View {
    var label: String
    @Binding var value: Int
    var range: ClosedRange<Int>
    var unit: String
    var zero: String? = nil
    @Environment(\.theme) var t
    var body: some View {
        HStack(spacing: 10) {
            Text(label).font(.mono(11)).frame(width: 190, alignment: .leading)
            Text("‹").font(.mono(13)).onTapGesture { value = max(range.lowerBound, value - 1) }
            Text(value == 0 && zero != nil ? zero! : "\(value) \(unit)").font(.mono(11, medium: true)).frame(width: 80)
            Text("›").font(.mono(13)).onTapGesture { value = min(range.upperBound, value + 1) }
        }
    }
}

func pickFolder(_ start: String? = nil) -> String? {
    let p = NSOpenPanel()
    p.canChooseDirectories = true; p.canChooseFiles = false; p.allowsMultipleSelection = false
    if let s = start { p.directoryURL = URL(fileURLWithPath: s) }
    return p.runModal() == .OK ? p.url?.path : nil
}

struct AppIcon: View {
    var path: String
    var body: some View {
        Image(nsImage: NSWorkspace.shared.icon(forFile: path)).resizable().frame(width: 18, height: 18)
    }
}

// MARK: - hc0 health

struct HealthView: View {
    @EnvironmentObject var cleaner: Cleaner
    @EnvironmentObject var startup: Startup
    @EnvironmentObject var nav: Nav
    @Environment(\.theme) var t
    @State var disk = Stats.disk()
    @State var mem = Stats.memory()
    @State var fda = Access.fullDisk
    let timer = Timer.publish(every: 3, on: .main, in: .common).autoconnect()

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                HStack(alignment: .top, spacing: 40) {
                    VStack(alignment: .leading, spacing: 8) {
                        Stat(label: "startup disk", value: "\(Fmt.bytes(disk.free)) free",
                             sub: "of \(Fmt.bytes(disk.total)) · \(Fmt.bytes(max(0, disk.important - disk.free))) purgeable")
                        SkewBar(value: 1 - Double(disk.free) / Double(max(1, disk.total)), height: 12).frame(width: 300)
                    }
                    VStack(alignment: .leading, spacing: 8) {
                        Stat(label: "memory", value: "\(Fmt.bytes(mem.used)) in use",
                             sub: "of \(Fmt.bytes(mem.total)) · \(Fmt.bytes(mem.compressed)) compressed")
                        SkewBar(value: Double(mem.used) / Double(max(1, mem.total)), height: 12).frame(width: 300)
                    }
                    Stat(label: "up", value: Stats.uptime, sub: "\(Stats.model) · \(Stats.os)")
                }
                Hairline(label: "check")
                HStack(spacing: 14) {
                    FLink(text: cleaner.busy ? "checking…" : "run health check", primary: true, disabled: cleaner.busy) {
                        Task { await cleaner.analyse(); await startup.load() }
                    }
                    if cleaner.analysed { FLink(text: "clean \(Fmt.bytes(cleaner.totalFound))") { nav.page = .clean } }
                    Text(cleaner.busy ? cleaner.status : "").font(.mono(11))
                }
                if !fda {
                    Panel {
                        HStack {
                            Text("full disk access is off — putz can't see the bin, safari or mail. turn it on for putz in privacy & security.")
                                .font(.mono(11))
                            Spacer()
                            FLink(text: "open settings") { Access.openSettings() }
                            FLink(text: "check again") { fda = Access.fullDisk }
                        }
                    }
                }
                if cleaner.analysed { findings }
                Hairline(label: "history", trailing: "\(Fmt.bytes(History.shared.total)) freed in \(History.shared.records.count) runs")
                ForEach(History.shared.records.prefix(8)) { r in
                    HStack {
                        Text(Fmt.date(r.date)).font(.mono(11)).foregroundStyle(t.dim).frame(width: 90, alignment: .leading)
                        Text(r.what).font(.mono(11))
                        Spacer()
                        Text("\(Fmt.count(r.items)) items").font(.mono(11)).foregroundStyle(t.dim)
                        Text(Fmt.bytes(r.bytes)).font(.mono(11, medium: true)).frame(width: 80, alignment: .trailing)
                    }
                }
            }
            .padding(.top, 8)
        }
        .onReceive(timer) { _ in disk = Stats.disk(); mem = Stats.memory() }
    }

    var findings: some View {
        let groups = RuleGroup.allCases.map { g -> (RuleGroup, Int64) in
            (g, cleaner.rules.filter { $0.group == g && cleaner.isOn($0) }.reduce(0) { $0 + (cleaner.results[$1.id]?.bytes ?? 0) })
        }.filter { $0.1 > 0 }
        let maxB = groups.map(\.1).max() ?? 1
        let skipped = cleaner.rules.compactMap { r in cleaner.results[r.id]?.skipped.map { (r.name, $0) } }
        let missing = startup.items.filter(\.vendorMissing)
        return VStack(alignment: .leading, spacing: 8) {
            Hairline(label: "found", trailing: Fmt.bytes(cleaner.totalFound))
            ForEach(groups, id: \.0) { g, b in
                HStack(spacing: 12) {
                    Text(g.rawValue).font(.mono(11)).frame(width: 90, alignment: .leading)
                    SkewBar(value: Double(b) / Double(maxB))
                    Text(Fmt.bytes(b)).font(.mono(11)).frame(width: 80, alignment: .trailing)
                }
            }
            if !skipped.isEmpty || !missing.isEmpty || Double(disk.free) / Double(max(1, disk.total)) < 0.1 {
                Hairline(label: "worth a look").padding(.top, 8)
                if Double(disk.free) / Double(max(1, disk.total)) < 0.1 {
                    note("less than 10% of the disk is free — the disk page shows where it went", go: .disk)
                }
                ForEach(skipped.prefix(6), id: \.0) { n, why in note("\(n): \(why)", go: .clean) }
                if !missing.isEmpty { note("\(missing.count) startup items point at programs that are gone", go: .startup) }
            }
        }
    }
    func note(_ s: String, go: Page) -> some View {
        HStack { Text("· " + s).font(.mono(11)); Spacer(); Text("→ \(go.code)").font(.mono(11)).onTapGesture { nav.page = go } }
    }
}

// MARK: - cl1 clean

struct CleanView: View {
    @EnvironmentObject var c: Cleaner
    @ObservedObject var prefs = Prefs.shared
    @Environment(\.theme) var t
    @State var open: Set<String> = []
    @State var confirm = false

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 4, pinnedViews: []) {
                    ForEach(RuleGroup.allCases, id: \.self) { g in
                        let rules = c.rules.filter { $0.group == g }
                        if !rules.isEmpty { groupView(g, rules) }
                    }
                }
                .padding(.trailing, 8)
            }
            HStack(spacing: 12) {
                FLink(text: "analyse", disabled: c.busy) { Task { await c.analyse() } }
                FLink(text: c.analysed ? "clean \(Fmt.bytes(c.totalFound))" : "clean", primary: true, disabled: c.busy) { confirm = true }
                if c.busy { FLink(text: "stop") { c.cancel() } }
                Footer(status: c.status, progress: c.busy ? c.progress : nil)
                Text(prefs.useTrash ? "→ bin" : "→ erased").font(.mono(10)).foregroundStyle(t.dim)
            }
            if !c.failures.isEmpty && !c.busy {
                Text(c.failures.prefix(3).joined(separator: "\n")).font(.mono(10)).foregroundStyle(t.dim).lineLimit(3)
            }
        }
        .alert(prefs.useTrash ? "move the found items to the bin?" : "erase the found items?", isPresented: $confirm) {
            Button("clean", role: .destructive) { Task { await c.clean() } }
            Button("cancel", role: .cancel) {}
        } message: {
            Text(c.analysed ? "\(Fmt.bytes(c.totalFound)) from the ticked rules. erased files don't go to the bin (settings → removal)."
                            : "putz will analyse the ticked rules, then clean them.")
        }
    }

    func groupView(_ g: RuleGroup, _ rules: [Rule]) -> some View {
        let onCount = rules.filter(c.isOn).count
        let bytes = rules.filter(c.isOn).reduce(0) { $0 + (c.results[$1.id]?.bytes ?? 0) }
        return VStack(alignment: .leading, spacing: 3) {
            HStack(spacing: 10) {
                SkewCheck(on: Binding(get: { onCount == rules.count }, set: { c.setGroup(g, $0) }), mixed: onCount > 0)
                Text(g.rawValue).font(.sans(15, medium: true))
                Rectangle().fill(t.ink.opacity(0.25)).frame(height: 1)
                Text(c.analysed ? Fmt.bytes(bytes) : "\(onCount) / \(rules.count)").font(.mono(11))
            }
            .padding(.top, 12).padding(.bottom, 4)
            ForEach(rules) { r in ruleRow(r) }
        }
    }

    func ruleRow(_ r: Rule) -> some View {
        let res = c.results[r.id]
        let isOpen = open.contains(r.id)
        let maxB = max(1, c.rules.compactMap { c.results[$0.id]?.bytes }.max() ?? 1)
        return VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 10) {
                SkewCheck(on: Binding(get: { c.isOn(r) }, set: { c.set(r, $0) }))
                Text(r.name).font(.mono(12)).frame(width: 220, alignment: .leading)
                    .onTapGesture { if res?.items.isEmpty == false { if isOpen { open.remove(r.id) } else { open.insert(r.id) } } }
                Text(r.note + (r.needsFullDisk ? " · full disk access" : "") + (r.how == .trash ? " · to bin" : ""))
                    .font(.sans(11)).foregroundStyle(t.dim).lineLimit(1)
                Spacer(minLength: 8)
                if let s = res?.skipped { Text(s).font(.mono(11)).foregroundStyle(t.dim) }
                else if let res {
                    SkewBar(value: Double(res.bytes) / Double(maxB), height: 7).frame(width: 90)
                    Text(res.items.first?.kind == "sql" ? res.items.first!.label : Fmt.bytes(res.bytes))
                        .font(.mono(11, medium: true)).frame(width: 74, alignment: .trailing)
                    Text(res.items.isEmpty ? "" : (isOpen ? "▾" : "▸")).font(.mono(10)).frame(width: 10)
                        .onTapGesture { if isOpen { open.remove(r.id) } else { open.insert(r.id) } }
                }
            }
            .opacity(c.isOn(r) ? 1 : 0.55)
            if isOpen, let res { items(res) }
        }
    }

    func items(_ res: RuleResult) -> some View {
        Panel {
            VStack(alignment: .leading, spacing: 3) {
                ForEach(res.items.prefix(300)) { f in
                    HStack(spacing: 8) {
                        SkewCheck(on: Binding(get: { !c.unpicked.contains(f.id) },
                                              set: { if $0 { c.unpicked.remove(f.id) } else { c.unpicked.insert(f.id) } }))
                        Text(f.kind == "cmd" ? "runs a command" : Fmt.tilde(f.path)).font(.mono(10)).lineLimit(1).truncationMode(.middle)
                        Spacer()
                        Text(f.kind == "sql" ? f.label : Fmt.bytes(f.bytes)).font(.mono(10))
                        if f.kind == "path" { Text("↗").font(.mono(10)).onTapGesture { Files.reveal(f.path) } }
                    }
                }
                if res.items.count > 300 { Text("… \(res.items.count - 300) more").font(.mono(10)).foregroundStyle(t.dim) }
            }
        }
        .padding(.leading, 28).padding(.vertical, 4)
    }
}

// MARK: - un2 apps

struct AppsView: View {
    @EnvironmentObject var u: Uninstaller
    @Environment(\.theme) var t
    @State var confirm = false

    var body: some View {
        HStack(alignment: .top, spacing: 24) {
            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    Field(placeholder: "filter", text: $u.filter).frame(width: 150)
                    Spacer()
                    Tabs(items: [(0, "name"), (1, "size"), (2, "unused")], selection: $u.sort)
                }
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 1) {
                        ForEach(u.shown) { a in row(a) }
                    }
                }
                Footer(status: u.status)
            }
            .frame(width: 430)
            detail
        }
        .task { if u.apps.isEmpty { await u.load() } }
        .alert("uninstall \(u.selected?.name ?? "")?", isPresented: $confirm) {
            Button("uninstall", role: .destructive) { Task { await u.uninstall() } }
            Button("cancel", role: .cancel) {}
        } message: {
            Text("the app and \(u.leftovers.filter(\.picked).count) ticked leftovers go to the bin; you can put them back from there.")
        }
    }

    func row(_ a: AppInfo) -> some View {
        let on = u.selected?.path == a.path
        return HStack(spacing: 8) {
            AppIcon(path: a.path)
            Text(a.name).font(.mono(11, medium: on)).lineLimit(1)
            Spacer()
            Text(Fmt.ago(a.lastUsed)).font(.mono(10)).foregroundStyle(t.dim).frame(width: 74, alignment: .trailing)
            Text(a.size.map(Fmt.bytes) ?? "…").font(.mono(11)).frame(width: 70, alignment: .trailing)
        }
        .padding(.vertical, 3).padding(.horizontal, 6)
        .background(on ? t.paper : Color.clear)
        .contentShape(Rectangle())
        .onTapGesture { Task { await u.select(a) } }
    }

    @ViewBuilder var detail: some View {
        if let a = u.selected {
            VStack(alignment: .leading, spacing: 10) {
                HStack(spacing: 14) {
                    Image(nsImage: NSWorkspace.shared.icon(forFile: a.path)).resizable().frame(width: 56, height: 56)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(a.name.lowercased()).font(.sans(22, medium: true))
                        Text("\(a.version) · \(a.bundleID)").font(.mono(10)).foregroundStyle(t.dim)
                        Text(Fmt.tilde(a.path)).font(.mono(10)).foregroundStyle(t.dim)
                    }
                }
                let lb = u.leftovers.filter(\.picked).reduce(0) { $0 + $1.bytes }
                HStack(spacing: 30) {
                    Stat(label: "app", value: a.size.map(Fmt.bytes) ?? "…")
                    Stat(label: "leftovers", value: u.findingLeftovers ? "…" : Fmt.bytes(lb))
                    Stat(label: "last used", value: Fmt.ago(a.lastUsed))
                }
                Hairline(label: "found with it", trailing: "\(u.leftovers.count)")
                ScrollView {
                    VStack(alignment: .leading, spacing: 3) {
                        ForEach($u.leftovers) { $l in
                            HStack(spacing: 8) {
                                SkewCheck(on: $l.picked)
                                Text(l.why).font(.mono(10)).foregroundStyle(t.dim).frame(width: 150, alignment: .leading)
                                Text(Fmt.tilde(l.path)).font(.mono(10)).lineLimit(1).truncationMode(.middle)
                                Spacer()
                                if l.admin { Text("admin").font(.mono(9)).foregroundStyle(t.dim) }
                                Text(Fmt.bytes(l.bytes)).font(.mono(10)).frame(width: 64, alignment: .trailing)
                                Text("↗").font(.mono(10)).onTapGesture { Files.reveal(l.path) }
                            }
                        }
                        if !u.findingLeftovers && u.leftovers.isEmpty { Text("nothing else found").font(.mono(11)).foregroundStyle(t.dim) }
                    }
                }
                HStack(spacing: 12) {
                    FLink(text: "uninstall", primary: true, disabled: u.findingLeftovers) { confirm = true }
                    FLink(text: "show in finder") { Files.reveal(a.path) }
                    if a.system { Text("installed by a package · asks for your password").font(.mono(10)).foregroundStyle(t.dim) }
                }
            }
        } else {
            VStack(alignment: .leading, spacing: 8) {
                Text("pick an app").font(.sans(18, medium: true))
                Text("putz looks for its caches, preferences, containers, launch agents, helpers and plug-ins of the same name, and moves all of it to the bin together.")
                    .font(.sans(12)).foregroundStyle(t.dim).frame(maxWidth: 380, alignment: .leading)
                FLink(text: "reload list") { Task { await u.load() } }
            }
        }
    }
}

// MARK: - st3 startup

struct StartupView: View {
    @EnvironmentObject var s: Startup
    @Environment(\.theme) var t
    @State var confirmRemove: LaunchItem? = nil

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 2) {
                    ForEach([LaunchItem.Domain.login, .agent, .globalAgent, .daemon], id: \.self) { d in
                        let list = s.items.filter { $0.domain == d }
                        if !list.isEmpty {
                            Hairline(label: ["login items", "agents", "agents · all users", "daemons · system"][[LaunchItem.Domain.login, .agent, .globalAgent, .daemon].firstIndex(of: d)!], trailing: "\(list.count)").padding(.top, 12).padding(.bottom, 4)
                            ForEach(list) { row($0) }
                        }
                    }
                }
            }
            HStack(spacing: 12) {
                FLink(text: "reload", disabled: s.busy) { Task { await s.load() } }
                FLink(text: "login items in settings") {
                    NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.LoginItems-Settings.extension")!)
                }
                Footer(status: s.log.first ?? s.status)
            }
        }
        .task { if s.items.isEmpty { await s.load() } }
        .alert("move \(confirmRemove?.label ?? "") to the bin?", isPresented: Binding(get: { confirmRemove != nil }, set: { if !$0 { confirmRemove = nil } })) {
            Button("remove", role: .destructive) { if let it = confirmRemove { Task { await s.remove(it) } } }
            Button("cancel", role: .cancel) {}
        } message: { Text("it's unloaded first. the app that installed it may put it back.") }
    }

    func row(_ it: LaunchItem) -> some View {
        HStack(spacing: 10) {
            Skew(skew: 0.6).fill(it.disabled ? Color.clear : t.ink)
                .overlay(Skew(skew: 0.6).stroke(t.ink.opacity(0.7), lineWidth: 1))
                .frame(width: 18, height: 10)
            VStack(alignment: .leading, spacing: 1) {
                Text(it.label).font(.mono(11, medium: true)).lineLimit(1)
                Text(Fmt.tilde(it.program)).font(.mono(10)).foregroundStyle(t.dim).lineLimit(1).truncationMode(.middle)
            }
            Spacer()
            Text([it.loaded ? "running" : nil, it.runAtLoad ? "at load" : nil, it.keepAlive ? "keep alive" : nil,
                  it.vendorMissing ? "program missing" : nil, it.disabled ? "disabled" : nil].compactMap { $0 }.joined(separator: " · "))
                .font(.mono(10)).foregroundStyle(t.dim)
            if it.domain != .login { FLink(text: it.disabled ? "enable" : "disable", disabled: s.busy) { Task { await s.toggle(it) } } }
            FLink(text: "remove", disabled: s.busy) { confirmRemove = it }
            if !it.path.isEmpty { Text("↗").font(.mono(11)).onTapGesture { Files.reveal(it.path) } }
        }
        .padding(.vertical, 3)
        .opacity(it.disabled ? 0.6 : 1)
    }
}
