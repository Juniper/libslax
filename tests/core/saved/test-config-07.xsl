<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:xutil="http://xml.libslax.org/xutil" version="1.0" extension-element-prefixes="xutil">
  <xsl:template match="/">
    <data>
      <xsl:variable name="slax" select="xutil:xml-to-slax(., &quot;1.3&quot;)"/>
      <string>
        <xsl:value-of select="$slax"/>
      </string>
      <unescaped>
        <xsl:value-of select="$slax" disable-output-escaping="yes"/>
      </unescaped>
      <xsl:variable name="out" select="xutil:slax-to-xml($slax)"/>
      <out>
        <xsl:copy-of select="$out"/>
      </out>
      <more>
        <xsl:copy-of select="xutil:slax-to-xml(&quot;&lt;hmmm&gt; 10; &lt;max&gt; 'condition';&quot;)/*"/>
      </more>
    </data>
  </xsl:template>
</xsl:stylesheet>
