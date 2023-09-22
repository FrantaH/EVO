nBit = 4

for i1 in range(2**nBit):
    for i2 in range(2**nBit):
        print('{0:04b}'.format(i1),'{0:04b}'.format(i2),' : ','{0:08b}'.format((i1*i2)))

# nBit = 8

# for i1 in range(2**nBit):
    # for i2 in range(2**nBit):
        # print('{0:08b}'.format(i1),'{0:08b}'.format(i2),' : ','{0:016b}'.format((i1*i2)))