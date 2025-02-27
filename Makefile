.PHONY: certs
certs:
	@echo "🔐 Generating SSL certificates..."
	
	@# Generate CA key and certificate
	openssl genrsa -out ca.key 4096
	openssl req -new -x509 -days 365 -key ca.key -out ca.crt -subj "/CN=Root CA"
	
	@# Generate server key
	openssl genrsa -out server.key 4096
	
	@# Generate server CSR and certificate
	openssl req -new -key server.key -out server.csr -config ssl.conf
	openssl x509 -req -days 365 -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server.crt -extensions v3_req -extfile ssl.conf
	
	@# Generate client key and certificate
	openssl genrsa -out client.key 4096
	openssl req -new -key client.key -out client.csr -config ssl.conf
	openssl x509 -req -days 365 -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out client.crt -extensions v3_req -extfile ssl.conf
	
	@# Clean up CSR files
	rm -f *.csr
	
	@echo "✅ Certificates generated successfully!"
	@echo "Generated files: ca.key, ca.crt, server.key, server.crt, client.key, client.crt"

.PHONY: server
server:
	sudo docker service create \
		--name grpc-server \
		--network grpc-net \
		--publish published=50051,target=50051 \
		amoghk04/grpc-server:latest
	
.PHONY: client
client:
	sudo docker run -it \
		-v $(PWD)/ca.crt:/app/ca.crt:ro \
		-v $(PWD)/client.crt:/app/client.crt:ro \
		-v $(PWD)/client.key:/app/client.key:ro \
		amoghk04/grpc-client:latest 192.168.29.101:50051