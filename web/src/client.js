const { HelloRequest, EmptyRequest, FileUploadRequest, FileDownloadRequest } = require('./service_pb.js');
const { GreeterClient, FileServiceClient } = require('./service_grpc_web_pb.js');

// withCredentials setting must match your CORS policy
const client = new GreeterClient("http://localhost:8080", null, {
  format: 'text',
  withCredentials: false  
});

const clientOptions = {
    format: 'text',
    withCredentials: false
};

const fileClient = new FileServiceClient("http://localhost:8080", null, clientOptions);

window.sayHello = function() {
  const request = new HelloRequest();
  const name = document.getElementById('name').value || 'World';
  request.setName(name);

  // Only set safe, essential headers
  const metadata = { 
    'x-grpc-web': '1',
    'grpc-timeout': '10S'
  };

  client.sayHello(request, metadata, (err, response) => {
    const output = document.getElementById('response');
    if (err) {
      console.error('Error:', err.code, err.message);
      output.innerText = `Error: ${err.message}`;
      return;
    }
    output.innerText = response.getMessage();
  });
};

window.sayHelloAgain = function() {
    const request = new HelloRequest();
    const name = document.getElementById('name').value || 'World';
    request.setName(name);
  
    // Only set safe, essential headers
    const metadata = { 
      'x-grpc-web': '1',
      'grpc-timeout': '10S'
    };
  
    client.sayHelloAgain(request, metadata, (err, response) => {
      const output = document.getElementById('response');
      if (err) {
        console.error('Error:', err.code, err.message);
        output.innerText = `Error: ${err.message}`;
        return;
      }
      output.innerText = response.getMessage();
    });
  };

  window.getStatus = function() {
    const request = new EmptyRequest();
    
    const metadata = { 
        'x-grpc-web': '1',
        'grpc-timeout': '10S'
      };

    client.status(request, metadata, (err, response) => {
        if (err) {
            console.error('Status Error:', err);
            document.getElementById('response').innerText = 
                `Error ${err.code}: ${err.message}`;
            return;
        }

        const status = {
            status: response.getStatus(),
            version: response.getVersion(),
            timestamp: response.getTimestamp()
        };
        
        document.getElementById('response').innerText = 
            `Status: ${status.status}\n` +
            `Version: ${status.version}\n` +
            `Timestamp: ${new Date(status.timestamp).toISOString()}`;
    });
};

window.uploadFile = function() {
    const fileInput = document.getElementById('uploadFile');
    const file = fileInput.files[0];
    if (!file) {
      alert('Please select a file first');
      return;
    }
    if (!file.name) {
      alert('Selected file has no valid name');
      return;
    }
  
    const reader = new FileReader();
    reader.onload = function(e) {
      // Verify that the file has been read
      if (!e.target.result) {
        console.error("FileReader result is empty");
        return;
      }
      // Convert file contents into a Uint8Array
      const content = new Uint8Array(e.target.result);
      
      // Create and populate the FileUploadRequest
      const request = new FileUploadRequest();
      request.setFilename(file.name);
      request.setContent(content);
  
      // Log the request object to see its structure
      console.log('Constructed FileUploadRequest:', request.toObject());
  
      const progressDiv = document.getElementById('uploadProgress');
      progressDiv.innerText = `Uploading ${file.name} (${file.size} bytes)...`;
  
      const metadata = {
        'x-grpc-web': '1',
        'grpc-timeout': '100S'
      };
  
      console.log('Sending upload request with metadata:', metadata);
      // Call the uploadFile RPC using fileClient
      fileClient.uploadFile(request, metadata, (err, response) => {
        if (err) {
          console.error('Upload Error:', err);
          progressDiv.innerText = `Upload Error: ${err.message}`;
          return;
        }
        progressDiv.innerText = `Upload successful: ${response.getMessage()}`;
      });
    };
  
    reader.onerror = function(err) {
      console.error('File Read Error:', err);
      document.getElementById('uploadProgress').innerText = 'Error reading file';
    };
  
    reader.readAsArrayBuffer(file);
  };

  window.downloadFile = function() {
    const downloadPathInput = document.getElementById('downloadPath');
    const filename = downloadPathInput.value;
    if (!filename) {
      alert('Please enter a file path');
      return;
    }
  
    const request = new FileDownloadRequest();
    request.setFilename(filename);
  
    const metadata = {
      'x-grpc-web': '1',
      'grpc-timeout': '100S'
    };
  
    const progressDiv = document.getElementById('downloadProgress');
    progressDiv.innerText = 'Downloading...';
  
    fileClient.downloadFile(request, metadata, (err, response) => {
      if (err) {
        console.error('Download Error:', err);
        progressDiv.innerText = `Download Error: ${err.message}`;
        return;
      }
  
      // Get the file content as a Uint8Array (assuming `getContent_asU8` exists)
      const content = response.getContent_asU8();
      const blob = new Blob([content], { type: 'application/octet-stream' });
      const url = window.URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = filename.split('/').pop() || 'downloaded_file';
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      window.URL.revokeObjectURL(url);
  
      progressDiv.innerText = 'Download Complete';
    });
  };