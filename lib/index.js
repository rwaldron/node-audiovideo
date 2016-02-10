var Emitter = require('events').EventEmitter;
var Readable = require('stream').Readable;
var binding = require('bindings')('capture.node');

var platform = process.platform;
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


function V4L2Camera() {
  var args = [null].concat([].slice.call(arguments));
  var ctor = binding.Camera.bind.apply(binding.Camera, args);
  return new ctor();
}

function Camera() {
  this.device = platform === 'darwin' ?
    binding :
    new V4L2Camera('/dev/video0');
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

Camera.OSX.prototype.close = function() {};



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

  state.device.start();
  state.device.capture(function(success) {
    // if (state.device.configGet().formatName === 'YUYV') {
    //   console.log(state.device.toRGB());
    //   cs.push(state.device.toRGB());
    // } else {
    //   console.log(state.device.toRGB());
    //   cs.push(state.device.toRGB());
    // }

    console.log('success: ', success);
    // console.log(state.device.configGet().formatName);
    // console.log(state.device.toRGB());
    cs.push(state.device.frameRaw());
    cs.push(null);
    state.device.stop();


// var rgba = {data: new Buffer(size * 4), width: width, height: height};
// for (var i = 0; i < size; i++) {
//     rgba.data[i * 4 + 0] = rgb[i * 3 + 0];
//     rgba.data[i * 4 + 1] = rgb[i * 3 + 1];
//     rgba.data[i * 4 + 2] = rgb[i * 3 + 2];
//     rgba.data[i * 4 + 3] = 255;
// }
    // var width = state.device.width;
    // var height = state.device.height;

    // console.error('Result', width, height);

    // switch (state.device.configGet().formatName) {
    //   case 'MJPG':
    //     next(state.device.toYUYV());
    //     break;

    //   case 'YUYV':
    //     var rgb = state.device.toRGB();
    //     var jpeg = new Jpeg(rgb, width, height, 'rgb');
    //     console.error('Encoding');
    //     jpeg.encode(function (image, error) {
    //       console.error('Encoded');
    //     });
    //     break;

    //   default:
    //     throw new Error('Invalid format: ' + state.device.configGet().formatName);
    // }

    // function next (res) {
    //   cs.push(res);
    //   cs.push(null);
    // }
  }.bind(this));
  return cs;
};

Camera.Linux.prototype.close = function() {
  priv.get(this).device.stop();
};

function exportCamera(options) {
  var camCtor = platform === 'darwin' ? Camera.OSX : Camera.Linux;
  return new camCtor(options);
}

exportCamera.OSX = Camera.OSX;
exportCamera.Linux = Camera.Linux;

module.exports = {
  Camera: exportCamera,
  Microphone: null,
  Speaker: null,
};
