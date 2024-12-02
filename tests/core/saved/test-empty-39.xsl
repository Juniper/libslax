<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax-ext slax">
  <xsl:param name="first" select="123123123123"/>
  <xsl:variable name="data-temp-1">
    <n>
      <xsl:value-of select="$first"/>
    </n>
    <n>
      <xsl:value-of select="56456456"/>
    </n>
    <n>
      <xsl:value-of select="789789"/>
    </n>
    <n>
      <xsl:value-of select="89789 + 512"/>
    </n>
    <n>
      <xsl:value-of select="987654321"/>
    </n>
    <n>
      <xsl:value-of select="8760000"/>
    </n>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="data" select="slax-ext:node-set($data-temp-1)"/>
  <xsl:template match="/">
    <data>
      <xsl:for-each select="$data/*">
        <test n="{.}">
          <out>
            <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:printf(&quot;%jH%12s&quot;, .)"/>
          </out>
          <out>
            <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:printf(&quot;%j{space}H%-12s&quot;, .)"/>
          </out>
          <out>
            <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:printf(&quot;%d + %jh%12s + %d&quot;, 128000, ., 256000)"/>
          </out>
          <out>
            <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:printf(&quot;%d + %j{space,whole,suffix=cool}h%-12s + %d&quot;, 128000, ., 256000)"/>
          </out>
        </test>
      </xsl:for-each>
      <xsl:for-each xmlns:slax="http://xml.libslax.org/slax" select="slax:build-sequence(0, 3)">
        <out>
          <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:printf(&quot;%jt{love:}%12d&quot;, substring(&quot;abcdef&quot;, 1, . * 2))"/>
        </out>
        <out>
          <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:printf(&quot;%jt{more:}%12s&quot;, substring(&quot;abcdef&quot;, 1, . * 2))"/>
        </out>
      </xsl:for-each>
    </data>
  </xsl:template>
</xsl:stylesheet>
