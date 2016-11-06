/**
* %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
* 
* Copyright (c) 2016 LIBSCAPI (http://crypto.biu.ac.il/SCAPI)
* This file is part of the SCAPI project.
* DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
* 
* Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
* and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
* 
* The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
* 
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
* FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
* 
* We request that any publication and/or code referring to and/or based on SCAPI contain an appropriate citation to SCAPI, including a reference to
* http://crypto.biu.ac.il/SCAPI.
* 
* Libscapi uses several open source libraries. Please see these projects for any further licensing issues.
* For more information , See https://github.com/cryptobiu/libscapi/blob/master/LICENSE.MD
*
* %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
* 
*/


#include "../include/mid_layer/OpenSSLSymmetricEnc.hpp"

void OpenSSLEncWithIVAbs::doConstruct() {
	//Check that the given prp name is a valid algorithm for this encryption scheme.
	//Open SSL provides implementations only for AES and TripleDES in CBC mode 
	//and only for AES in CTR mode.
	if (!checkExistance(prpName)) {
		throw invalid_argument("The given prp name is not supported in this encryption mode.");
	}

	//Create native objects for encryption and decryption.
	enc = EVP_CIPHER_CTX_new();
	dec = EVP_CIPHER_CTX_new();

	//Set the other parameters.
	this->prpName = prpName;
}

OpenSSLEncWithIVAbs::~OpenSSLEncWithIVAbs() {
	EVP_CIPHER_CTX_cleanup(enc);
	EVP_CIPHER_CTX_cleanup(dec);
	EVP_CIPHER_CTX_free(enc);
	EVP_CIPHER_CTX_free(dec);
}

/**
* Supply the encryption scheme with a Secret Key.
*/
void OpenSSLEncWithIVAbs::setKey(SecretKey & secretKey) {
	int len = secretKey.getEncoded().size() * 8;

	//The key size should suit the encryption type. 
	//In case the underlying prp is TripleDES, the key should be 128 or 192 bits long.
	//In case the underlying prp is AES, the key should be 128, 192 or 256 bits long.
	if (len != 128 && len != 192) {
		if ((len != 256) && (prpName == "AES")) {
			throw InvalidKeyException("AES key size should be 128/192/256 bits long");
		}
		else if (prpName == "TripleDES") {
			throw InvalidKeyException("TripleDES key size should be 128/192 bits long");
		}
	}

	keySet = true;
}

/**
* Generates a secret key to initialize the underlying PRP object.
* @param keySize is the required secret key size in bits.
* @return the generated secret key.
*/
SecretKey OpenSSLEncWithIVAbs::generateKey(int keySize) {
	//Creates a byte array of size keySize.
	vector<byte> genBytes(keySize / 8);

	//Generates the bytes using the random.
	random->getPRGBytes(genBytes, 0, keySize / 8);

	//Creates a secretKey from the generated bytes.
	return SecretKey(genBytes, "");
}

/**
* This function encrypts a plaintext. It lets the system choose the random IV.
* @param plaintext should be an instance of ByteArrayPlaintext.
* @return  an IVCiphertext, which contains the IV used and the encrypted data.
* @throws IllegalStateException if no secret key was set.
* @throws invalid_argument if the given plaintext is not an instance of ByteArrayPlaintext.
*/
shared_ptr<SymmetricCiphertext> OpenSSLEncWithIVAbs::encrypt(Plaintext* plaintext) {
	//Allocate space for the IV.
	int size = getIVSize();
	vector<byte> iv(size);
	//Generate a random IV.
	random->getPRGBytes(iv, 0, size);

	//Encrypt the plaintext with the just chosen random IV.
	return encrypt(plaintext, iv);
}

/**
* This function encrypts a plaintext. It lets the user choose the random IV.
* @param plaintext should be an instance of ByteArrayPlaintext.
* @param iv random bytes to use in the encryption of the message.
* @return an IVCiphertext, which contains the IV used and the encrypted data.
*/
shared_ptr<SymmetricCiphertext> OpenSSLEncWithIVAbs::encrypt(Plaintext* plaintext, vector<byte> & iv) {
	if (!keySet) {
		throw IllegalStateException("no SecretKey was set");
	}
	//Check validity of IV's length:
	if ((int) iv.size() != getIVSize()) {
		throw invalid_argument("The length of the IV passed is not equal to the block size of current PRP");
	}

	auto plain = dynamic_cast<ByteArrayPlaintext*>(plaintext);
	if (plain == NULL) {
		throw invalid_argument("plaintext should be instance of ByteArrayPlaintext");
	}

	//Call the native function that idoes the encryption. 
	auto plainT = plain->getText();
	vector<byte> cipher = encryptOpenSSL(plainT, iv);

	//Create and return an IVCiphertext with the iv and encrypted data.
	return make_shared<IVCiphertext>(make_shared<ByteArraySymCiphertext>(cipher), iv);
}

/**
* Decrypts the given ciphertext using the underlying prp as the block cipher function.
*
* @param ciphertext the given ciphertext to decrypt. MUST be an instance of IVCiphertext.
* @return the plaintext object containing the decrypted ciphertext.
*/
shared_ptr<Plaintext> OpenSSLEncWithIVAbs::decrypt(SymmetricCiphertext* ciphertext) {

	if (!keySet) {
		throw IllegalStateException("no SecretKey was set");
	}

	//If the ciphertext is not of type IVCiphertext - throw exception.
	auto ivCipher = dynamic_cast<IVCiphertext*>(ciphertext);
	if (ivCipher == NULL) {
		throw invalid_argument("The ciphertext has to be of type IVCiphertext");
	}

	//Gets the iv and the ciphertext bytes from the IVCiphertext parameters.
	auto iv = ivCipher->getIv();
	auto cipher = ivCipher->getBytes();

	//Call the native funcrion that does the decryption.
	auto plaintext = decryptOpenSSL(cipher, iv);

	return make_shared<ByteArrayPlaintext>(plaintext);
}

vector<byte> OpenSSLEncWithIVAbs::encryptOpenSSL(vector<byte> & plaintext, vector<byte> & iv) {
	
	//Initialize the encryption objects with the key.
	EVP_EncryptInit(enc, NULL, NULL, (unsigned char*)iv.data());
		
	int blockSize = EVP_CIPHER_CTX_block_size(enc);

	//Before the encryption, tha plaintext should be padded.
	//The padding scheme aligns the plaintext to size blockSize (and if the plaintext already aligned, it add an entire blockSize bytes.
	//As a result, the size of the ciphertext should be at most of size plaintextSize + blockSize.
	vector<byte> out(plaintext.size() + blockSize);

	int size, rem;

	//Encrypt the plaintext.
	EVP_EncryptUpdate(enc, (unsigned char*)out.data(), &size, (unsigned char*)plaintext.data(), plaintext.size());
	EVP_EncryptFinal_ex(enc, (unsigned char*)out.data() + size, &rem);

	//shrink the output to the actual size.
	out.resize(size + rem);
	
	return out;
}

vector<byte> OpenSSLEncWithIVAbs::decryptOpenSSL(vector<byte> & cipher, vector<byte> & iv) {

	//Initialize the encryption object with the key.
	EVP_DecryptInit(dec, NULL, NULL, (unsigned char*)iv.data());

	//Allocate a new byte array of size cipherSize.
	vector<byte> out(cipher.size());

	int size, rem;

	//Decrypt the ciphertext.
	EVP_DecryptUpdate(dec, (unsigned char*)out.data(), &size, (unsigned char*)cipher.data(), cipher.size());

	EVP_DecryptFinal_ex(dec, (unsigned char*)out.data() + size, &rem);

	//shrink the output to the actual size.
	out.resize(size + rem); //TODO delete?

	return out;
}