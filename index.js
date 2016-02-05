var Readable = require('stream').Readable;
var v4l2 = require('./v4l2');

function Camera(devPath) {
  this.device = new v4l2.Camera(devPath || "/dev/video0");
  this.device.start();
}

Camera.prototype.captureShot = function() {
  var out = new Readable();
  out._read = function () { };

  this.device.capture(function () {
    var formatName = this.device.configGet().formatName;

    switch (formatName) {
      case 'MJPG':
        next(this.device.toYUYV());
        break;

      case 'YUYV':
        next(this.device.toRGB());
        break;

      default:
        throw new Error('Invalid format: ' + formatName);
    }

    function next(res) {
      out.push(res);
      out.push(null);
    }
  }.bind(this));

  return out;
}

Camera.prototype.close = function () {
  this.device.stop();
}

module.exports = Camera;
