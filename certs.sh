mkdir -p certs
cd certs

echo "Generating SSL certificates without passphrase..."

# 1. Generate CA key and certificate
echo "Generating CA certificates..."
openssl genrsa -out ca.key 2048
openssl req -new -x509 -key ca.key -out ca.crt -days 365 \
    -subj "/C=US/ST=California/L=SanFrancisco/O=MyOrg/CN=MyCA"

# 2. Generate Server key and certificate
echo "Generating Server certificates..."
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr \
    -subj "/C=US/ST=California/L=SanFrancisco/O=MyOrg/CN=localhost"
openssl x509 -req -days 365 -in server.csr -CA ca.crt -CAkey ca.key -set_serial 01 -out server.crt

# 3. Generate Client key and certificate
echo "Generating Client certificates..."
openssl genrsa -out client.key 2048
openssl req -new -key client.key -out client.csr \
    -subj "/C=US/ST=California/L=SanFrancisco/O=MyOrg/CN=client"
openssl x509 -req -days 365 -in client.csr -CA ca.crt -CAkey ca.key -set_serial 02 -out client.crt

# Set correct permissions
chmod 644 *.crt
chmod 644 *.key

echo "Certificate generation complete!"
echo "Verifying certificates..."
openssl verify -CAfile ca.crt server.crt
openssl verify -CAfile ca.crt client.crt

cd ..