import QtQuick 2.6

Column {
    id: chart
    property var points: []
    property string metric
    property string title
    spacing: 4
    Text { text: chart.title; color: "#cbd5e0" }
    Canvas {
        id: canvas
        width: parent.width; height: 110
        property real upper: 1
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = "#111827"
            ctx.fillRect(0, 0, width, height)
            var rows = chart.points
            var maximum = 1
            for (var i = 0; i < rows.length; ++i) {
                var value = rows[i][chart.metric]
                if (typeof value === "number" && isFinite(value)) maximum = Math.max(maximum, value)
            }
            upper = maximum * 1.1
            if (!rows.length) return
            var first = rows[0].seconds
            var span = Math.max(0.001, rows[rows.length - 1].seconds - first)
            ctx.strokeStyle = "#64d2ff"
            ctx.lineWidth = 1.5
            ctx.beginPath()
            var connected = false
            for (var j = 0; j < rows.length; ++j) {
                var row = rows[j]
                var yValue = row[chart.metric]
                if (typeof yValue !== "number" || !isFinite(yValue)) { connected = false; continue }
                var x = 5 + (row.seconds - first) / span * (width - 10)
                var y = height - 5 - yValue / upper * (height - 10)
                if (row.gap || !connected) ctx.moveTo(x, y)
                else ctx.lineTo(x, y)
                // A dot keeps isolated fresh frames and single samples visible.
                ctx.fillStyle = "#64d2ff"
                ctx.fillRect(x - 2, y - 2, 4, 4)
                connected = true
            }
            ctx.stroke()
        }
    }
    Text { text: "Y: 0 … " + canvas.upper.toFixed(2) + " · X: " + (chart.points.length ? chart.points[0].seconds.toFixed(1) + " … " + chart.points[chart.points.length - 1].seconds.toFixed(1) : "—") + " s"; color: "#8e9bb0"; font.pixelSize: 10 }
    onPointsChanged: canvas.requestPaint()
}
