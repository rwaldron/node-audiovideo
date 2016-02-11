# audiovideo

Cross-platform audio record/playback and video recording.

Works on OS X, and Linux. Supports Tessel 2.

```
npm install audiovideo
```

## Example

```js
var av = require('audiovideo');
var fs = require('fs');
var Camera = av.Camera;

var camera = new Camera();
var capture = camera.capture();

capture.on('data', function(data) {
  fs.writeFileSync('captures/captured-via-data-event.jpg', data);
});

// or...

capture.pipe(fs.createWriteStream('captures/captured-via-pipe.jpg'));
```

## License

MIT or ASL2, at your option.
