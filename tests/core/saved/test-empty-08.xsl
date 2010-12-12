<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="/">
    <!-- 
     * This turns out to be mostly useless, since libxslt
     * gives an error when a script with an unknown element
     * if run.  It should probably check for a fallback before
     * giving the error.
 -->
    <xsl:some-new-feature select="nothing">
      <xsl:fallback>
        <xsl:message>some-new-feature not implemented</xsl:message>
      </xsl:fallback>
    </xsl:some-new-feature>
  </xsl:template>
</xsl:stylesheet>
