var cp = require('child_process');
var fs = require('fs');

var Emitter = require('events').EventEmitter;
var Readable = require('stream').Readable;

var platform = process.platform;
var binding = platform === 'darwin' && require('bindings')('capture.node');
var priv = new Map();

function CaptureStream() {
  Readable.call(this);
  this._read = function() {};
}

CaptureStream.prototype = Object.create(Readable.prototype, {
  constructor: {
    value: CaptureStream
  }
});

// function V4L2Camera() {
//   var args = [null].concat([].slice.call(arguments));
//   var ctor = binding.Camera.bind.apply(binding.Camera, args);
//   return new ctor();
// }

function FSWebcam(options) {
  this.path = options.devpath || '/dev/video0';
  this.quality = options.quality || 80;
}

FSWebcam.prototype.capture = function(callback) {
  var fswebcam = cp.spawn('fswebcam', [
    '-i', '0', '-d', 'v4l2:' + this.path, '--no-banner', '--jpeg', this.quality, '--save', '/tmp/capture.jpg'
  ]);

  fswebcam.on('close', function(code) {
    var error = code === 0 ? null : error;

    if (error) {
      callback(error);
    } else {
      fs.readFile('/tmp/capture.jpg', callback);
    }
  });
};

function Camera(options) {
  this.device = platform === 'darwin' ?
    binding :
    new FSWebcam(options);

  this.width = options.width;
  this.height = options.height;
}

Camera.OSX = function(options) {
  Emitter.call(this);
  priv.set(this, new Camera(options));
};

Camera.OSX.prototype = Object.create(Emitter.prototype, {
  constructor: {
    value: Camera.OSX
  }
});

Camera.OSX.prototype.capture = function() {
  var state = priv.get(this);
  var cs = new CaptureStream();

  cs.on('data', function(result) {
    this.emit('data', result);
  }.bind(this));

  cs.on('end', function(result) {
    this.emit('end', result);
  }.bind(this));

  setImmediate(function() {
    cs.push(state.device.capture());
    cs.push(null);
  });
  return cs;
};

Camera.Linux = function(options) {
  Emitter.call(this);
  priv.set(this, new Camera(options));
};

Camera.Linux.prototype = Object.create(Emitter.prototype, {
  constructor: {
    value: Camera.Linux
  }
});

Camera.Linux.prototype.capture = function() {
  var state = priv.get(this);
  var cs = new CaptureStream();

  cs.on('data', function(result) {
    this.emit('data', result);
  }.bind(this));

  cs.on('end', function(result) {
    this.emit('end', result);
  }.bind(this));

  state.device.capture(function(error, data) {
    cs.push(data);
    cs.push(null);
  });
  return cs;
};

var exportables = {};
var defaults = {
  width: 320,
  height: 240,
};

exportables.Camera = function(options) {
  var camCtor = platform === 'darwin' ? Camera.OSX : Camera.Linux;

  return new camCtor(Object.assign({}, defaults, options || {}));
};

exportables.Camera.OSX = Camera.OSX;
exportables.Camera.Linux = Camera.Linux;

module.exports = {
  Camera: exportables.Camera,
  Microphone: null,
  Speaker: null,
};
