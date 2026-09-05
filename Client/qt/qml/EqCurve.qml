import QtQuick
import Aural

// Smoothed response-curve preview of the current EQ state (clear bass + 5 bands).
Canvas {
    id: curve

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        var w = width, h = height;
        var vals = [Hp.eqClearBass, Hp.eqBands[0], Hp.eqBands[1], Hp.eqBands[2], Hp.eqBands[3], Hp.eqBands[4]];
        var n = vals.length;
        function px(i){ return (i/(n-1))*w; }
        function py(v){ return h/2 - (v/12)*(h/2 - 6); }
        // baseline
        ctx.strokeStyle = Theme.outlineVariant;
        ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(0,h/2); ctx.lineTo(w,h/2); ctx.stroke();
        // fill under curve
        ctx.beginPath();
        ctx.moveTo(0, py(vals[0]));
        for (var i=1;i<n;i++){ var xc=(px(i-1)+px(i))/2; ctx.bezierCurveTo(xc,py(vals[i-1]),xc,py(vals[i]),px(i),py(vals[i])); }
        ctx.lineTo(w,h); ctx.lineTo(0,h); ctx.closePath();
        ctx.fillStyle = Qt.rgba(0.69,0.55,1.0,0.12);
        ctx.fill();
        // curve line
        ctx.beginPath();
        ctx.moveTo(0, py(vals[0]));
        for (var j=1;j<n;j++){ var xc2=(px(j-1)+px(j))/2; ctx.bezierCurveTo(xc2,py(vals[j-1]),xc2,py(vals[j]),px(j),py(vals[j])); }
        ctx.strokeStyle = Theme.primary;
        ctx.lineWidth = 2.5;
        ctx.stroke();
    }

    Connections { target: Hp; function onEqChanged(){ curve.requestPaint() } }
    Connections { target: Theme; function onDarkChanged(){ curve.requestPaint() } }
    Component.onCompleted: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
}
