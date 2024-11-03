blockSize=2
h=[-1.14, -0.08, 1.49, -0.79, -1.38, -4.73, 1.9, -4.41, 2.63, 4.26]
hBlocks=length(h)/blockSize
x=[3, -1, 0, 3, 2, 0, 1, 2, 1, 8, 8, 8, 1, 2, 3, 4]


NFFT = blockSize * 2;
X_fdl = zeros(NFFT / 2 + 1, hBlocks); % input blocks frequency delay line
H = zeros(NFFT / 2 + 1, hBlocks); % impulse response blocks FFT results
x_tdl = zeros(NFFT, 1); % temp buffer for FFT

%% split very long impulse response into small blocks and convert to FFT domain
startIdx = 1;
endIdx = blockSize;
for hBlock = 1:hBlocks
    %H(:, hBlock) = fftr(h(startIdx:endIdx), NFFT);
    hpart = h(startIdx:endIdx)
    H(:, hBlock) = fft(h(startIdx:endIdx), NFFT)(1:NFFT/2+1)
    startIdx = endIdx + 1;
    endIdx = endIdx + blockSize;
end
    
disp('Start processing')
    
%% process signal stream x frame by frame and apply UPOLS
startIdx = 1;
endIdx = blockSize;

while endIdx <= length(x)
    % 1. right half of the input buffer is shifted to the left
    x_tdl(1:blockSize) = x_tdl(blockSize + 1:end); 
    
    % and the new block is stored in the right half
    x_tdl(blockSize + 1:end) = x(startIdx:endIdx); 
    
    % 2. circular shift X_fdl, so that 1st column is current block
    X_fdl = circshift(X_fdl, [0, 1]); 

    % 3. take FFT for current block input
    %X_fdl(:, 1) = fftr(x_tdl, NFFT);
    X_fdl(:, 1) = fft(x_tdl, NFFT)(1:NFFT/2+1);
    
    X_fdl
    H
    
    % 4. point-wise multiplication and accumulate the result
    Y = sum(X_fdl .* H, 2);
    
    % 5. take IFFT for current block output
    %yifft = ifft(Y, NFFT);
    
    yifft = ifft(cat(1,Y,conj(Y(end-1:-1:2))), NFFT);
    
    % left half is discarded and the right half is returned
    y(startIdx:endIdx) = yifft(blockSize + 1:end);
    
    % update loop variable
    startIdx = endIdx + 1;
    endIdx = endIdx + blockSize;
end

y

conv = conv(h,x)
