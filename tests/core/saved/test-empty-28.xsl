<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:bit="http://xml.libslax.org/bit" version="1.0" extension-element-prefixes="bit">
  <xsl:variable name="a" select="63"/>
  <xsl:variable name="b">
    <xsl:text>10111</xsl:text>
  </xsl:variable>
  <xsl:template match="/">
    <out>
      <bit-set>
        <a1>
          <xsl:value-of select="bit:set(&quot;10000&quot;, 0)"/>
        </a1>
        <a2>
          <xsl:value-of select="bit:set(&quot;10000&quot;, 3)"/>
        </a2>
        <a3>
          <xsl:value-of select="bit:set(&quot;10000&quot;, 5)"/>
        </a3>
        <a4>
          <xsl:value-of select="bit:set(&quot;10000&quot;, 8)"/>
        </a4>
      </bit-set>
      <bit-clear>
        <a1>
          <xsl:value-of select="bit:clear(&quot;11111&quot;, 0)"/>
        </a1>
        <a2>
          <xsl:value-of select="bit:clear(&quot;11111&quot;, 3)"/>
        </a2>
        <a3>
          <xsl:value-of select="bit:clear(&quot;11111&quot;, 5)"/>
        </a3>
        <a4>
          <xsl:value-of select="bit:clear(&quot;11111&quot;, 8)"/>
        </a4>
      </bit-clear>
      <bit-compare>
        <a1>
          <xsl:value-of select="bit:compare(13, 14)"/>
        </a1>
        <a2>
          <xsl:value-of select="bit:compare(14, 12)"/>
        </a2>
        <a3>
          <xsl:value-of select="bit:compare(16, 15)"/>
        </a3>
        <a4>
          <xsl:value-of select="bit:compare(&quot;10000&quot;, 16)"/>
        </a4>
        <a5>
          <xsl:value-of select="bit:compare(&quot;11111&quot;, &quot;10000&quot;)"/>
        </a5>
        <a6>
          <xsl:value-of select="bit:compare(&quot;10101&quot;, &quot;101010&quot;)"/>
        </a6>
        <a7>
          <xsl:value-of select="bit:compare(&quot;101010&quot;, &quot;10101&quot;)"/>
        </a7>
      </bit-compare>
      <bit-and>
        <a1>
          <xsl:value-of select="bit:and(&quot;10&quot;, &quot;01&quot;)"/>
        </a1>
        <a2>
          <xsl:value-of select="bit:and(&quot;101100&quot;, &quot;100101&quot;)"/>
        </a2>
        <a3>
          <xsl:value-of select="bit:and(&quot;0110&quot;, &quot;1010101&quot;)"/>
        </a3>
        <a4>
          <xsl:value-of select="bit:and(&quot;11111111&quot;, &quot;1111&quot;)"/>
        </a4>
        <a5>
          <xsl:value-of select="bit:and(&quot;111&quot;, &quot;1111111&quot;)"/>
        </a5>
        <a6>
          <xsl:value-of select="bit:and(&quot;101&quot;, &quot;1111111&quot;)"/>
        </a6>
        <a7>
          <xsl:value-of select="bit:and(&quot;10111&quot;, &quot;1111101&quot;)"/>
        </a7>
        <a8>
          <xsl:value-of select="bit:and(256 + 8, 256 + 8 + 1)"/>
        </a8>
        <a9>
          <xsl:value-of select="bit:and($a, $b)"/>
        </a9>
        <a10>
          <xsl:value-of select="bit:and($a, number($b))"/>
        </a10>
      </bit-and>
      <bit-or>
        <a1>
          <xsl:value-of select="bit:or(&quot;10&quot;, &quot;01&quot;)"/>
        </a1>
        <a2>
          <xsl:value-of select="bit:or(&quot;101100&quot;, &quot;100101&quot;)"/>
        </a2>
        <a3>
          <xsl:value-of select="bit:or(&quot;0110&quot;, &quot;1010101&quot;)"/>
        </a3>
        <a4>
          <xsl:value-of select="bit:or(&quot;11111111&quot;, &quot;1111&quot;)"/>
        </a4>
        <a5>
          <xsl:value-of select="bit:or(&quot;111&quot;, &quot;1111111&quot;)"/>
        </a5>
        <a6>
          <xsl:value-of select="bit:or(&quot;101&quot;, &quot;1111111&quot;)"/>
        </a6>
        <a7>
          <xsl:value-of select="bit:or(&quot;10111&quot;, &quot;1111101&quot;)"/>
        </a7>
        <a8>
          <xsl:value-of select="bit:or(256 + 8, 256 + 8 + 1)"/>
        </a8>
        <a9>
          <xsl:value-of select="bit:or($a, $b)"/>
        </a9>
        <a10>
          <xsl:value-of select="bit:or($a, number($b))"/>
        </a10>
      </bit-or>
      <bit-mask>
        <a1>
          <xsl:value-of select="bit:mask(0)"/>
        </a1>
        <a2>
          <xsl:value-of select="bit:mask(1)"/>
        </a2>
        <a3>
          <xsl:value-of select="bit:mask(4)"/>
        </a3>
        <a4>
          <xsl:value-of select="bit:mask(4, 8)"/>
        </a4>
        <a5>
          <xsl:value-of select="bit:mask(8, 32)"/>
        </a5>
        <a6>
          <xsl:value-of select="bit:mask(10, 8)"/>
        </a6>
        <a7>
          <xsl:value-of select="bit:mask(3, 3)"/>
        </a7>
        <a8>
          <xsl:value-of select="bit:mask(7, 3)"/>
        </a8>
        <a9>
          <xsl:value-of select="bit:mask(3, 7)"/>
        </a9>
        <a10>
          <xsl:value-of select="bit:not(bit:mask(3, 7))"/>
        </a10>
      </bit-mask>
      <ops>
        <a1>
          <xsl:value-of select="bit:to-int(&quot;10101&quot;)"/>
        </a1>
        <a2>
          <xsl:value-of select="bit:from-int(65535)"/>
        </a2>
        <a3>
          <xsl:value-of select="bit:from-int(15, 10)"/>
        </a3>
        <a4>
          <xsl:value-of select="bit:to-hex(&quot;10101&quot;)"/>
        </a4>
        <a5>
          <xsl:value-of select="bit:from-hex(&quot;0x345&quot;)"/>
        </a5>
      </ops>
    </out>
  </xsl:template>
</xsl:stylesheet>
