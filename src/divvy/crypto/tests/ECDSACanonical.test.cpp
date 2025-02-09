//------------------------------------------------------------------------------
/*
    This file is part of divvyd: https://github.com/xdv/divvyd
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <divvy/crypto/ECDSACanonical.h>
#include <beast/unit_test/suite.h>
#include <algorithm>
#include <iterator>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>

namespace divvy {

class ECDSACanonical_test : public beast::unit_test::suite
{
public:
    template <class FwdIter, class Container>
    static
    void
    hex_to_binary (FwdIter first, FwdIter last, Container& out)
    {
        struct Table
        {
            int val[256];
            Table ()
            {
                std::fill (val, val+256, 0);
                for (int i = 0; i < 10; ++i)
                    val ['0'+i] = i;
                for (int i = 0; i < 6; ++i)
                {
                    val ['A'+i] = 10 + i;
                    val ['a'+i] = 10 + i;
                }
            }
            int operator[] (int i)
            {
               return val[i];
            }
        };

        static Table lut;
        out.reserve (std::distance (first, last) / 2);
        while (first != last)
        {
            auto const hi (lut[(*first++)]);
            auto const lo (lut[(*first++)]);
            out.push_back ((hi*16)+lo);
        }
    }
    Blob loadSignature (std::string const& hex)
    {
        Blob b;
        hex_to_binary (hex.begin (), hex.end (), b);
        return b;
    }

    // Verifies that a signature is syntactically valid.
    bool isValid (std::string const& hex)
    {
        Blob j (loadSignature(hex));
        return isCanonicalECDSASig (&j[0], j.size(), ECDSA::not_strict);
    }

    // Verifies that a signature is syntactically valid and in canonical form.
    bool isStrictlyCanonical (std::string const& hex)
    {
        Blob j (loadSignature(hex));
        return isCanonicalECDSASig (&j[0], j.size (), ECDSA::strict);
    }

    // Verify that we correctly identify strictly canonical signatures
    void testStrictlyCanonicalSignatures ()
    {
        testcase ("Strictly canonical signature checks", abort_on_fail);

        expect (isStrictlyCanonical("3045"
            "022100FF478110D1D4294471EC76E0157540C2181F47DEBD25D7F9E7DDCCCD47EEE905"
            "0220078F07CDAE6C240855D084AD91D1479609533C147C93B0AEF19BC9724D003F28"),
            "Strictly canonical signature");

        expect (isStrictlyCanonical("3045"
            "0221009218248292F1762D8A51BE80F8A7F2CD288D810CE781D5955700DA1684DF1D2D"
            "022041A1EE1746BFD72C9760CC93A7AAA8047D52C8833A03A20EAAE92EA19717B454"),
            "Strictly canonical signature");

        expect (isStrictlyCanonical("3044"
            "02206A9E43775F73B6D1EC420E4DDD222A80D4C6DF5D1BEECC431A91B63C928B7581"
            "022023E9CC2D61DDA6F73EAA6BCB12688BEB0F434769276B3127E4044ED895C9D96B"),
            "Strictly canonical signature");

         expect (isStrictlyCanonical("3044"
            "022056E720007221F3CD4EFBB6352741D8E5A0968D48D8D032C2FBC4F6304AD1D04E"
            "02201F39EB392C20D7801C3E8D81D487E742FA84A1665E923225BD6323847C71879F"),
            "Strictly canonical signature");

        expect (isStrictlyCanonical("3045"
            "022100FDFD5AD05518CEA0017A2DCB5C4DF61E7C73B6D3A38E7AE93210A1564E8C2F12"
            "0220214FF061CCC123C81D0BB9D0EDEA04CD40D96BF1425D311DA62A7096BB18EA18"),
            "Strictly canonical signature");

        // These are canonical signatures, but *not* strictly canonical.
        expect (!isStrictlyCanonical ("3046"
            "022100F477B3FA6F31C7CB3A0D1AD94A231FDD24B8D78862EE334CEA7CD08F6CBC0A1B"
            "022100928E6BCF1ED2684679730C5414AEC48FD62282B090041C41453C1D064AF597A1"),
            "Not strictly canonical signature");

        expect (!isStrictlyCanonical ("3045"
            "022063E7C7CA93CB2400E413A342C027D00665F8BAB9C22EF0A7B8AE3AAF092230B6"
            "0221008F2E8BB7D09521ABBC277717B14B93170AE6465C5A1B36561099319C4BEB254C"),
            "Not strictly canonical signature");

        expect (!isStrictlyCanonical ("3046"
            "02210099DCA1188663DDEA506A06A7B20C2B7D8C26AFF41DECE69D6C5F7C967D32625F"
            "022100897658A6B1F9EEE5D140D7A332DA0BD73BB98974EA53F6201B01C1B594F286EA"),
            "Not strictly canonical signature");

        expect (!isStrictlyCanonical ("3045"
            "02200855DE366E4E323AA2CE2A25674401A7D11F72EC432770D07F7B57DF7387AEC0"
            "022100DA4C6ADDEA14888858DE2AC5B91ED9050D6972BB388DEF582628CEE32869AE35"),
            "Not strictly canonical signature");
    }

    // Verify that we correctly identify valid signatures
    void testValidSignatures ()
    {
        testcase ("Canonical signature checks", abort_on_fail);

        // r and s 1 byte 1
        expect (isValid ("3006"
            "020101"
            "020102"),
            "Well-formed short signature");

        expect (isValid ("3044"
            "02203932c892e2e550f3af8ee4ce9c215a87f9bb831dcac87b2838e2c2eaa891df0c"
            "022030b61dd36543125d56b9f9f3a1f53189e5af33cdda8d77a5209aec03978fa001"),
            "Canonical signature");

        expect (isValid ("3045"
            "0220076045be6f9eca28ff1ec606b833d0b87e70b2a630f5e3a496b110967a40f90a"
            "0221008fffd599910eefe00bc803c688eca1d2ba7f6b180620eaa03488e6585db6ba01"),
            "Canonical signature");

        expect (isValid("3046"
            "022100876045be6f9eca28ff1ec606b833d0b87e70b2a630f5e3a496b110967a40f90a"
            "0221008fffd599910eefe00bc803c688c2eca1d2ba7f6b180620eaa03488e6585db6ba"),
            "Canonical signature");

        expect (isStrictlyCanonical("3045"
            "022100FF478110D1D4294471EC76E0157540C2181F47DEBD25D7F9E7DDCCCD47EEE905"
            "0220078F07CDAE6C240855D084AD91D1479609533C147C93B0AEF19BC9724D003F28"),
            "Canonical signature");

        expect (isStrictlyCanonical("3045"
            "0221009218248292F1762D8A51BE80F8A7F2CD288D810CE781D5955700DA1684DF1D2D"
            "022041A1EE1746BFD72C9760CC93A7AAA8047D52C8833A03A20EAAE92EA19717B454"),
            "Canonical signature");

        expect (isStrictlyCanonical("3044"
            "02206A9E43775F73B6D1EC420E4DDD222A80D4C6DF5D1BEECC431A91B63C928B7581"
            "022023E9CC2D61DDA6F73EAA6BCB12688BEB0F434769276B3127E4044ED895C9D96B"),
            "Canonical signature");

         expect (isStrictlyCanonical("3044"
            "022056E720007221F3CD4EFBB6352741D8E5A0968D48D8D032C2FBC4F6304AD1D04E"
            "02201F39EB392C20D7801C3E8D81D487E742FA84A1665E923225BD6323847C71879F"),
            "Canonical signature");

        expect (isStrictlyCanonical("3045"
            "022100FDFD5AD05518CEA0017A2DCB5C4DF61E7C73B6D3A38E7AE93210A1564E8C2F12"
            "0220214FF061CCC123C81D0BB9D0EDEA04CD40D96BF1425D311DA62A7096BB18EA18"),
            "Canonical signature");
    }

    // Verify that we correctly identify malformed or invalid signatures
    void testMalformedSignatures ()
    {
        testcase ("Non-canonical signature checks", abort_on_fail);

        expect (!isValid("3005"
            "0201FF"
            "0200"),
            "tooshort");

        expect (!isValid ("3006"
            "020101"
            "020202"),
            "Slen-Overlong");

        expect (!isValid ("3006"
            "020701"
            "020102"),
            "Rlen-Overlong-OOB");

        expect (!isValid ("3006"
            "020401"
            "020102"),
            "Rlen-Overlong-OOB");

        expect (!isValid ("3006"
            "020501"
            "020102"),
            "Rlen-Overlong-OOB");

        expect (!isValid ("3006"
            "020201"
            "020102"),
            "Rlen-Overlong");

        expect (!isValid ("3006"
            "020301"
            "020202"),
            "Rlen Overlong and Slen-Overlong");

        expect (!isValid ("3006"
            "020401"
            "020202"),
            "Rlen Overlong and OOB and Slen-Overlong");

        expect (!isValid("3047"
            "0221005990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "022200002d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "toolong");

        expect (!isValid("3144"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "type");

        expect (!isValid("3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "totallength");

        expect (!isValid("301F"
            "01205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1"),
            "Slenoob");

        expect (!isValid("3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed00"),
            "R+S");

        expect (!isValid("3044"
            "01205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Rtype");

        expect (!isValid("3024"
            "0200"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Rlen=0");

        expect (!isValid("3044"
            "02208990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "R<0");

        expect (!isValid("3045"
            "0221005990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "02202d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Rpadded");

        expect (!isValid("3044"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105012"
            "02d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Stype");

        expect (!isValid("3024"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "0200"),
            "Slen=0");

        expect (!isValid("3044"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "0220fd5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "S<0");

        expect (!isValid("3045"
            "02205990e0584b2b238e1dfaad8d6ed69ecc1a4a13ac85fc0b31d0df395eb1ba6105"
            "0221002d5876262c288beb511d061691bf26777344b702b00f8fe28621fe4e566695ed"),
            "Spadded");
    }

    void convertNonCanonical(std::string const& hex, std::string const& canonHex)
    {
        Blob b (loadSignature(hex));

        // The signature ought to at least be valid before we begin.
        expect (isValid (hex), "invalid signature");

        size_t len = b.size ();

        expect (!makeCanonicalECDSASig (&b[0], len),
            "non-canonical signature was already canonical");

        expect (b.size () >= len,
            "canonicalized signature length longer than non-canonical");

        b.resize (len);

        expect (isCanonicalECDSASig (&b[0], len, ECDSA::strict),
            "canonicalized signature is not strictly canonical");

        Blob canonicalForm (loadSignature (canonHex));

        expect (b.size () == canonicalForm.size (),
            "canonicalized signature doesn't have the expected length");

        expect (std::equal (b.begin (), b.end (), canonicalForm.begin ()),
            "canonicalized signature isn't what we expected");
    }

    // Verifies correctness of non-canonical to canonical conversion
    void testCanonicalConversions()
    {
        testcase ("Non-canonical signature canonicalization", abort_on_fail);

        convertNonCanonical (
            "3046"
                "022100F477B3FA6F31C7CB3A0D1AD94A231FDD24B8D78862EE334CEA7CD08F6CBC0A1B"
                "022100928E6BCF1ED2684679730C5414AEC48FD62282B090041C41453C1D064AF597A1",
            "3045"
                "022100F477B3FA6F31C7CB3A0D1AD94A231FDD24B8D78862EE334CEA7CD08F6CBC0A1B"
                "02206D719430E12D97B9868CF3ABEB513B6EE48C5A361F4483FA7A9641868540A9A0");

        convertNonCanonical (
            "3045"
                "022063E7C7CA93CB2400E413A342C027D00665F8BAB9C22EF0A7B8AE3AAF092230B6"
                "0221008F2E8BB7D09521ABBC277717B14B93170AE6465C5A1B36561099319C4BEB254C",
            "3044"
                "022063E7C7CA93CB2400E413A342C027D00665F8BAB9C22EF0A7B8AE3AAF092230B6"
                "022070D174482F6ADE5443D888E84EB46CE7AFC8968A552D69E5AF392CF0844B1BF5");

        convertNonCanonical (
            "3046"
                "02210099DCA1188663DDEA506A06A7B20C2B7D8C26AFF41DECE69D6C5F7C967D32625F"
                "022100897658A6B1F9EEE5D140D7A332DA0BD73BB98974EA53F6201B01C1B594F286EA",
            "3045"
                "02210099DCA1188663DDEA506A06A7B20C2B7D8C26AFF41DECE69D6C5F7C967D32625F"
                "02207689A7594E06111A2EBF285CCD25F4277EF55371C4F4AA1BA4D09CD73B43BA57");

        convertNonCanonical (
            "3045"
                "02200855DE366E4E323AA2CE2A25674401A7D11F72EC432770D07F7B57DF7387AEC0"
                "022100DA4C6ADDEA14888858DE2AC5B91ED9050D6972BB388DEF582628CEE32869AE35",
            "3044"
                "02200855DE366E4E323AA2CE2A25674401A7D11F72EC432770D07F7B57DF7387AEC0"
                "022025B3952215EB7777A721D53A46E126F9AD456A2B76BAB0E399A98FA9A7CC930C");
    }

    void run ()
    {
        testValidSignatures ();
        testStrictlyCanonicalSignatures ();
        testMalformedSignatures ();
        testCanonicalConversions ();
    }
};

BEAST_DEFINE_TESTSUITE(ECDSACanonical,sslutil,divvy);

}
