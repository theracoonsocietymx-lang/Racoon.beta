// Ventana de progreso del instalador de Racoon.
// "Instalar Racoon.command" la compila con swiftc y la abre junto a la Terminal. Lee dos archivos:
//   estado.txt  una línea "base|tramo|texto": la barra va de base a base+tramo durante ese paso (0 a 1 en total)
//   paso.log    lo que imprime el paso; si trae porcentajes de compilación ("[ 45%]"), la barra avanza con ellos
// El texto FIN cierra la ventana a los pocos segundos; ERROR la deja abierta con el aviso.
// Si la Terminal se cierra (el proceso del instalador ya no existe), la ventana se cierra sola.
// Uso: progreso <estado.txt> <paso.log> <pid del instalador>
import AppKit

let args = CommandLine.arguments
let statusPath = args.count > 1 ? args[1] : ""
let logPath = args.count > 2 ? args[2] : ""
let installerPid: Int32 = args.count > 3 ? (Int32(args[3]) ?? 0) : 0

final class Progreso: NSObject, NSApplicationDelegate {
    var window: NSWindow!
    let titulo = NSTextField(labelWithString: "Instalando Racoon")
    let paso = NSTextField(labelWithString: "Preparando")
    let detalle = NSTextField(labelWithString: "La primera vez tarda bastante. Puedes seguir usando tu Mac.")
    let barra = NSProgressIndicator()
    var timer: Timer?
    var terminado = false
    var maximo = 0.0
    var pasoActual = ""
    var compilando = false   // los porcentajes cuentan después de "Build files have been written" (antes son de juceaide) o de "== avance =="
    let porcentaje = try? NSRegularExpression(pattern: "\\[ *([0-9]{1,3})%\\]", options: [])

    func applicationDidFinishLaunching(_ notification: Notification) {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 540, height: 168),
                          styleMask: [.titled, .closable, .miniaturizable],
                          backing: .buffered, defer: false)
        window.title = "Racoon"
        window.isReleasedWhenClosed = false
        let v = NSView(frame: NSRect(x: 0, y: 0, width: 540, height: 168))

        titulo.font = NSFont.boldSystemFont(ofSize: 18)
        titulo.frame = NSRect(x: 24, y: 120, width: 492, height: 26)
        paso.font = NSFont.systemFont(ofSize: 13)
        paso.frame = NSRect(x: 24, y: 92, width: 492, height: 20)
        paso.lineBreakMode = .byTruncatingTail
        barra.isIndeterminate = false
        barra.minValue = 0
        barra.maxValue = 1
        barra.doubleValue = 0
        barra.style = .bar
        barra.frame = NSRect(x: 24, y: 62, width: 492, height: 20)
        detalle.font = NSFont.systemFont(ofSize: 11)
        detalle.textColor = NSColor.secondaryLabelColor
        detalle.frame = NSRect(x: 24, y: 24, width: 492, height: 30)
        detalle.lineBreakMode = .byWordWrapping
        detalle.maximumNumberOfLines = 2

        for c in [titulo, paso, barra, detalle] as [NSView] { v.addSubview(c) }
        window.contentView = v
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)

        timer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in self?.refrescar() }
        refrescar()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { return true }

    func ultimoPorcentaje() -> Double? {
        guard let h = FileHandle(forReadingAtPath: logPath) else { return nil }
        defer { h.closeFile() }
        let fin = h.seekToEndOfFile()
        h.seek(toFileOffset: fin > 8192 ? fin - 8192 : 0)
        let data = h.readDataToEndOfFile()
        guard let texto = String(data: data, encoding: .utf8) ?? String(data: data, encoding: .isoLatin1),
              let re = porcentaje else { return nil }
        var ns = texto as NSString
        var marca = ns.range(of: "Build files have been written", options: .backwards)
        if marca.location == NSNotFound { marca = ns.range(of: "== avance ==", options: .backwards) }
        if marca.location != NSNotFound {
            compilando = true
            ns = ns.substring(from: marca.location) as NSString
        }
        if !compilando { return nil }
        let hallados = re.matches(in: ns as String, options: [], range: NSRange(location: 0, length: ns.length))
        guard let ultimo = hallados.last, ultimo.numberOfRanges > 1,
              let n = Double(ns.substring(with: ultimo.range(at: 1))) else { return nil }
        return min(100.0, n) / 100.0
    }

    func refrescar() {
        if terminado { return }
        if installerPid > 0 && kill(installerPid, 0) != 0 {   // la Terminal se cerró
            NSApp.terminate(nil)
            return
        }
        guard let s = try? String(contentsOfFile: statusPath, encoding: .utf8) else { return }
        let partes = s.trimmingCharacters(in: .whitespacesAndNewlines).components(separatedBy: "|")
        guard partes.count >= 3, let base = Double(partes[0]), let tramo = Double(partes[1]) else { return }
        let texto = partes[2...].joined(separator: "|")

        if texto.hasPrefix("FIN") {
            terminado = true
            barra.doubleValue = 1
            titulo.stringValue = "Racoon quedó instalado"
            paso.stringValue = "Está en Aplicaciones con los 11 plugins. Tu DAW también los ve."
            detalle.stringValue = "Esta ventana se cierra sola."
            DispatchQueue.main.asyncAfter(deadline: .now() + 6) { NSApp.terminate(nil) }
            return
        }
        if texto.hasPrefix("ERROR") {
            terminado = true
            titulo.stringValue = "Algo falló"
            paso.stringValue = "Copia el texto de la ventana de Terminal y mándalo."
            detalle.stringValue = "Puedes cerrar esta ventana."
            return
        }
        if texto != pasoActual { pasoActual = texto; compilando = false }
        let dentro = ultimoPorcentaje() ?? 0
        let valor = min(1.0, max(0.0, base + tramo * dentro))
        maximo = max(maximo, valor)          // la barra nunca se regresa
        barra.doubleValue = maximo
        paso.stringValue = texto + "  ·  " + String(Int(maximo * 100)) + " %"
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
let delegado = Progreso()
app.delegate = delegado
app.run()
